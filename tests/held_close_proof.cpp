/* SPDX-License-Identifier: GPL-2.0-or-later */
// Held-callback close proof.  One accepted S callback is held inside the
// consumer packet sink while the real EnclosureContext close misses its
// deadline: the S family must retain every owned object (no early destruction)
// and only destroy once the callback settles.  This is the S-side counterpart
// to the P early-reset hazard.
#include "siano-capture-consumer.h"
#include "siano-enclosure.h"

#include "px4/posix_tuner_nonce.h"

extern "C" {
#include "output-writer.h"
#include "protocol.h"
}

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" int test_libusb_deliver(struct libusb_transfer*, const std::uint8_t*,
                                   std::size_t);
extern "C" int test_libusb_pending(struct libusb_transfer*);
extern "C" int test_libusb_deliver_control_to(struct libusb_transfer*, std::uint16_t);

namespace siano::capture {
struct ConsumerTestAccess {
    static void sink_gate(Consumer& consumer, void (*gate)(void*) noexcept, void* context)
    {
        consumer.run_->test_sink_gate = gate;
        consumer.run_->test_sink_context = context;
    }
};
}  // namespace siano::capture

namespace {
std::atomic<int> g_family_destroy_begin{0};
std::atomic<int> g_family_destroy_end{0};
}  // namespace

extern "C" void siano_proof_family_destroy_begin() { g_family_destroy_begin.fetch_add(1); }
extern "C" void siano_proof_family_destroy_end() { g_family_destroy_end.fetch_add(1); }

namespace {

using namespace dtv::userland;
using namespace dtv::userland::detail;
using namespace siano::runtime;
namespace posix = px4::userland::ipc::posix;

pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
std::atomic<bool> gate_entered{false};
std::atomic<bool> gate_released{false};

void sink_gate(void*) noexcept
{
    gate_entered.store(true);
    pthread_mutex_lock(&gate_mutex);
    while (!gate_released.load()) pthread_cond_wait(&gate_cond, &gate_mutex);
    pthread_mutex_unlock(&gate_mutex);
}

bool deliver_ts(siano_device& device, std::uint8_t marker, std::size_t packets)
{
    std::vector<std::uint8_t> payload(packets * SIANO_OUTPUT_PACKET);
    for (std::size_t i = 0; i < packets; ++i) {
        auto* packet = payload.data() + i * SIANO_OUTPUT_PACKET;
        std::memset(packet, marker, SIANO_OUTPUT_PACKET);
        packet[0] = 0x47U;
        packet[1] = 0x01U;
        packet[2] = static_cast<std::uint8_t>(marker + i);
        packet[3] = 0x10U;
    }
    std::vector<std::uint8_t> message(SMS_HEADER_SIZE + payload.size());
    sms_pack_header(message.data(), MSG_SMS_DVBT_BDA_DATA, 0U, 0U,
                    static_cast<std::uint16_t>(message.size()), 0U);
    std::memcpy(message.data() + SMS_HEADER_SIZE, payload.data(), payload.size());
    for (std::size_t i = 0U; i < MAX_URBS; ++i) {
        if (device.transfers[i] && test_libusb_pending(device.transfers[i]))
            return test_libusb_deliver(device.transfers[i], message.data(), message.size()) == 0;
    }
    return false;
}

std::string make_private_dir()
{
    std::array<char, 64U> templ{};
    std::snprintf(templ.data(), templ.size(), "/tmp/siano-held-XXXXXX");
    char* created = mkdtemp(templ.data());
    if (created == nullptr) return {};
    if (chmod(created, 0700) != 0) return {};
    return std::string(created);
}

}  // namespace

int main()
{
    const std::string runtime_dir = make_private_dir();
    assert(!runtime_dir.empty());
    SianoRuntimeConfig config;
    config.usb = nullptr;
    config.own_usb = false;
    config.selector = siano_device_selector{};
    config.selector.kind = SIANO_DEVICE_SELECTOR_PORT;
    config.selector.bus = 1U;
    config.selector.ports[0] = 2U;
    config.selector.ports[1] = 3U;
    config.selector.port_count = 2U;
    config.selector_text = "usb:1-2.3";
    config.queue_packets = 64U;

    posix::PosixTunerNonceSource epoch_source;
    const auto epoch = epoch_source.generate();
    assert(epoch);
    auto assembly = assemble_siano_context(config, "heldclose0001", runtime_dir.c_str(),
                                           epoch.value(), "0.0.0-dev");
    if (!assembly) {
        std::fprintf(stderr, "HELD assemble_error=%s\n", error_string(assembly.error()));
        return 1;
    }
    posix::EnclosureContext& owned = *assembly.value().context;
    SianoFamily* family = assembly.value().family;
    siano_device& device = family->adapter().device();

    Attachment attachment{};
    attachment.owner_client_id = 1U;
    attachment.lease_id = 1U;
    attachment.attachment_id = 1U;
    attachment.receiver = 0U;
    attachment.system = ReceiverSystem::terrestrial;
    attachment.evict_oldest = true;
    if (!family->adapter().attach(attachment)) {
        std::fprintf(stderr, "HELD attach failed\n");
        return 1;
    }
    siano::capture::ConsumerTestAccess::sink_gate(family->consumer(), sink_gate, nullptr);

    for (std::size_t i = 0U; i < MAX_URBS; ++i) {
        for (unsigned w = 0U; w < 3000U && !test_libusb_pending(device.transfers[i]); ++w)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (test_libusb_deliver_control_to(device.transfers[i],
                                           MSG_SMS_SIGNAL_DETECTED_IND) != 0) {
            std::fprintf(stderr, "HELD signal delivery failed\n");
            return 1;
        }
        for (unsigned w = 0U; w < 3000U && !test_libusb_pending(device.transfers[i]); ++w)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!deliver_ts(device, 0x55U, 1U)) { std::fprintf(stderr, "HELD deliver failed\n"); return 1; }
    for (unsigned i = 0U; i < 3000U && !gate_entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!gate_entered.load()) { std::fprintf(stderr, "HELD gate not entered\n"); return 1; }

    // A callback is held: close misses its deadline and must retain the graph.
    const posix::ContextCloseResult early = owned.close(std::chrono::milliseconds(200));
    if (early.quiesced) { std::fprintf(stderr, "HELD close did not expire\n"); return 1; }
    if (g_family_destroy_begin.load() == 0 || g_family_destroy_end.load() != 0) {
        std::fprintf(stderr, "HELD owner destroyed while held begin=%d end=%d\n",
                     g_family_destroy_begin.load(), g_family_destroy_end.load());
        if (!gate_released.exchange(true)) pthread_cond_broadcast(&gate_cond);
        return 1;
    }
    std::fprintf(stderr, "HELD expired quiesced=0 family_begin=%d family_end=%d\n",
                 g_family_destroy_begin.load(), g_family_destroy_end.load());

    // Release the held callback: cleanup settles and destroys each owner once.
    {
        pthread_mutex_lock(&gate_mutex);
        gate_released.store(true);
        pthread_cond_broadcast(&gate_cond);
        pthread_mutex_unlock(&gate_mutex);
    }
    const posix::ContextCloseResult settled = owned.close(std::chrono::milliseconds(15000));
    const posix::ContextCloseResult again = owned.close(std::chrono::milliseconds(1000));
    // This proof bypasses the logical lease/stream protocol and attaches the
    // consumer directly, so the tuner shutdown worker legitimately reports
    // BUSY from the still-attached accepted adapter.  The retention/one-time
    // destruction invariants are what this proof exercises.
    if (!settled.quiesced) {
        std::fprintf(stderr, "HELD settle quiesced=0 error=%s\n",
                     error_string(settled.error));
        return 1;
    }
    if (g_family_destroy_begin.load() != 1 || g_family_destroy_end.load() != 1 ||
        !again.quiesced) {
        std::fprintf(stderr, "HELD destroy counts begin=%d end=%d again_quiesced=%d\n",
                     g_family_destroy_begin.load(), g_family_destroy_end.load(),
                     again.quiesced ? 1 : 0);
        return 1;
    }
    std::fprintf(stderr,
                 "HELD settled quiesced=1 error=%s begin=1 end=1 again_quiesced=1\n",
                 error_string(settled.error));
    return 0;
}
