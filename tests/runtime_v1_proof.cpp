/* SPDX-License-Identifier: GPL-2.0-or-later */
// Limited acceptance proof for the Siano public runtime.  The real assembly
// (same function as sianod) is served over the real AF_UNIX server, and the
// real siano-ts client runs as a separate process.  Only the bottom USB layer
// is replaced by the loopback fixture.
#include "siano-enclosure.h"

#include "px4/posix_ipc_contract.h"
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
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifndef SIANO_TS_PATH
#error "SIANO_TS_PATH must name the built siano-ts client"
#endif
#ifndef SIANOCTL_PATH
#error "SIANOCTL_PATH must name the built sianoctl client"
#endif
#ifndef SIANO_RUNTIME_VERSION
#define SIANO_RUNTIME_VERSION "0.0.0-dev"
#endif

extern "C" int test_libusb_deliver(struct libusb_transfer*, const std::uint8_t*,
                                   std::size_t);
extern "C" int test_libusb_pending(struct libusb_transfer*);
extern "C" unsigned test_libusb_tune_requests(void);
extern "C" std::uint32_t test_libusb_last_tune_hz(void);
extern "C" unsigned test_libusb_control_requests(void);
extern "C" int test_libusb_deliver_control_to(struct libusb_transfer*, std::uint16_t);

namespace {

using namespace dtv::userland;
using namespace dtv::userland::detail;
using namespace siano::runtime;
namespace posix = px4::userland::ipc::posix;

constexpr std::size_t kPackets = 5U;

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

bool wait_transfer_pending(struct libusb_transfer* transfer)
{
    for (unsigned i = 0U; i < 3000U; ++i) {
        if (test_libusb_pending(transfer)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::vector<std::uint8_t> expected_ts(std::uint8_t marker, std::size_t packets)
{
    std::vector<std::uint8_t> expected(packets * SIANO_OUTPUT_PACKET);
    for (std::size_t i = 0; i < packets; ++i) {
        auto* packet = expected.data() + i * SIANO_OUTPUT_PACKET;
        std::memset(packet, marker, SIANO_OUTPUT_PACKET);
        packet[0] = 0x47U;
        packet[1] = 0x01U;
        packet[2] = static_cast<std::uint8_t>(marker + i);
        packet[3] = 0x10U;
    }
    return expected;
}

std::vector<std::uint8_t> read_file(const std::string& path)
{
    std::vector<std::uint8_t> bytes;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return bytes;
    std::uint8_t buffer[4096];
    std::size_t got = 0U;
    while ((got = std::fread(buffer, 1U, sizeof(buffer), file)) > 0U)
        bytes.insert(bytes.end(), buffer, buffer + got);
    std::fclose(file);
    return bytes;
}

int run_client(const std::string& runtime_dir, const std::string& instance,
               const std::string& output, const char* time_or_count,
               const char* time_value, bool explicit_runtime_dir = true)
{
    pid_t pid = fork();
    if (pid == 0) {
        const char* argv[16];
        int index = 0;
        argv[index++] = SIANO_TS_PATH;
        if (std::strcmp(time_or_count, "time") == 0) {
            argv[index++] = "-c";
            argv[index++] = "T13";
            argv[index++] = "-t";
            argv[index++] = time_value;
        } else {
            argv[index++] = "-c";
            argv[index++] = "T13";
            argv[index++] = "--packet-count";
            argv[index++] = time_value;
        }
        argv[index++] = "--instance";
        argv[index++] = instance.c_str();
        if (explicit_runtime_dir) {
            argv[index++] = "--runtime-dir";
            argv[index++] = runtime_dir.c_str();
        }
        argv[index++] = "-o";
        argv[index++] = output.c_str();
        argv[index++] = "--tune-timeout-ms";
        argv[index++] = "5000";
        argv[index] = nullptr;
        execv(SIANO_TS_PATH, const_cast<char* const*>(argv));
        _exit(127);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int run_status(const std::string& runtime_dir, const std::string& instance,
               std::string& output)
{
    std::fprintf(stderr, "PROOF status_argv=%s --instance %s --runtime-dir %s status\n",
                 SIANOCTL_PATH, instance.c_str(), runtime_dir.c_str());
    int output_pipe[2];
    if (pipe(output_pipe) != 0) return -1;
    const pid_t pid = fork();
    if (pid == 0) {
        close(output_pipe[0]);
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0) _exit(127);
        close(output_pipe[1]);
        const char* argv[] = {SIANOCTL_PATH, "--instance", instance.c_str(),
                              "--runtime-dir", runtime_dir.c_str(), "status", nullptr};
        execv(SIANOCTL_PATH, const_cast<char* const*>(argv));
        _exit(127);
    }
    close(output_pipe[1]);
    std::array<char, 4096U> buffer{};
    ssize_t count = 0;
    while ((count = read(output_pipe[0], buffer.data(), buffer.size())) > 0)
        output.append(buffer.data(), static_cast<std::size_t>(count));
    close(output_pipe[0]);
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

bool check_status(std::string_view json, const std::string& instance,
                  const std::array<std::uint8_t, 16U>& epoch, const char* receiver_state)
{
    constexpr char hex[] = "0123456789abcdef";
    std::array<char, 33U> epoch_text{};
    for (std::size_t i = 0U; i < epoch.size(); ++i) {
        epoch_text[i * 2U] = hex[(epoch[i] >> 4U) & 15U];
        epoch_text[i * 2U + 1U] = hex[epoch[i] & 15U];
    }
    const std::string expected_epoch(epoch_text.data(), 32U);
    return json.size() > 2U && json.front() == '{' && json.back() == '\n' &&
           json.find("\"name\":\"userland.status\"") != std::string_view::npos &&
           json.find("\"product\":\"siano-userland\"") != std::string_view::npos &&
           json.find("\"version\":\"0.0.0-dev\"") != std::string_view::npos &&
           json.find("\"epoch\":\"" + expected_epoch + "\"") != std::string_view::npos &&
           json.find("\"instance\":\"" + instance + "\"") != std::string_view::npos &&
           json.find("\"descriptor\":{\"receiver\":0") != std::string_view::npos &&
           json.find("\"systems\":[\"ISDB-T\"]") != std::string_view::npos &&
           json.find(std::string("\"state\":\"") + receiver_state + "\"") !=
               std::string_view::npos &&
           json.find("\"measurements\":{}") != std::string_view::npos &&
           json.find("\"readers\":[]") != std::string_view::npos;
}

std::string make_private_dir()
{
    std::array<char, 64U> templ{};
    std::snprintf(templ.data(), templ.size(), "/tmp/siano-proof-XXXXXX");
    char* created = mkdtemp(templ.data());
    if (created == nullptr) return {};
    if (chmod(created, 0700) != 0) return {};
    return std::string(created);
}

SianoRuntimeConfig base_config()
{
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
    return config;
}

}  // namespace

int main()
{
    // 1. The published LIST is the adopted shape: one terrestrial receiver 0
    //    and an empty reader set (no external CARD).
    const std::string runtime_dir = make_private_dir();
    assert(!runtime_dir.empty());
    const std::string instance = "proofinst0001";
    const SianoRuntimeConfig config = base_config();

    posix::PosixTunerNonceSource epoch_source;
    const auto epoch = epoch_source.generate();
    assert(epoch);

    auto assembly = assemble_siano_context(config, instance, runtime_dir.c_str(),
                                           epoch.value(), SIANO_RUNTIME_VERSION);
    if (!assembly) {
        std::fprintf(stderr, "assemble_error=%s\n", error_string(assembly.error()));
        return 1;
    }
    posix::EnclosureContext& owned = *assembly.value().context;
    SianoFamily* family = assembly.value().family;
    assert(family != nullptr);
    siano_device& device = family->adapter().device();

    px4::userland::ipc::posix::ControlContext control{};
    const auto facts_result = resolve_rio_facts(nullptr, config.selector, config.selector_text);
    assert(facts_result);
    assert(build_siano_control_context(facts_result.value(), instance, epoch.value(),
                                       SIANO_RUNTIME_VERSION, owned.tuner_service(), control));
    assert(control.list.readers.present && control.list.readers.value.count == 0U);
    assert(control.list.receivers.present && control.list.receivers.value.count == 1U);
    assert(control.list.receivers.value.items[0].systems.mask == 1U);
    std::fprintf(stderr, "PROOF list readers=%u receivers=%u\n",
                 static_cast<unsigned>(control.list.readers.value.count),
                 static_cast<unsigned>(control.list.receivers.value.count));

    std::atomic<bool> serving{true};
    std::thread server([&]() {
        while (serving.load()) {
            const auto polled = owned.poll_once(Timeout{50U});
            if (!polled) {
                std::fprintf(stderr, "PROOF server poll error=%u\n",
                             static_cast<unsigned>(polled.error()));
                break;
            }
        }
    });

    std::string idle_status;
    const unsigned idle_controls = test_libusb_control_requests();
    if (run_status(runtime_dir, instance, idle_status) != 0 ||
        !check_status(idle_status, instance, epoch.value(), "free") ||
        test_libusb_control_requests() != idle_controls) {
        std::fprintf(stderr, "PROOF idle status failed: %s", idle_status.c_str());
        return 1;
    }
    std::fprintf(stderr, "PROOF idle_status=%s", idle_status.c_str());

    // 2. Real client process over the real AF_UNIX endpoints; bottom USB is
    //    the loopback.  A feeder injects SIGNAL_DETECTED and K BDA packets
    //    once the consumer is attached.
    std::atomic<bool> fed{false};
    std::thread feeder([&]() {
        for (unsigned i = 0U; i < 5000U && !family->stream_attached(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!family->stream_attached()) {
            std::fprintf(stderr, "PROOF attach wait expired tune_requests=%u\n",
                         test_libusb_tune_requests());
            return;
        }
        std::string streaming_status;
        const unsigned controls_before_status = test_libusb_control_requests();
        if (run_status(runtime_dir, instance, streaming_status) != 0 ||
            !check_status(streaming_status, instance, epoch.value(), "streaming") ||
            test_libusb_control_requests() != controls_before_status) {
            std::fprintf(stderr, "PROOF streaming status failed controls=%u/%u: %s",
                         controls_before_status, test_libusb_control_requests(),
                         streaming_status.c_str());
            return;
        }
        std::fprintf(stderr, "PROOF streaming_status=%s", streaming_status.c_str());
        for (std::size_t i = 0U; i < MAX_URBS; ++i) {
            if (!wait_transfer_pending(device.transfers[i])) {
                std::fprintf(stderr, "PROOF transfer not pending index=%zu\n", i);
                return;
            }
            if (test_libusb_deliver_control_to(device.transfers[i],
                                               MSG_SMS_SIGNAL_DETECTED_IND) != 0) {
                std::fprintf(stderr, "PROOF signal delivery failed index=%zu\n", i);
                return;
            }
            if (!wait_transfer_pending(device.transfers[i])) {
                std::fprintf(stderr, "PROOF transfer resubmit missing index=%zu\n", i);
                return;
            }
        }
        fed.store(deliver_ts(device, 0x41U, kPackets));
        if (!fed.load()) std::fprintf(stderr, "PROOF TS delivery failed\n");
    });

    const std::string output_path = runtime_dir + "/capture.ts";
    const int client_status = run_client(runtime_dir, instance, output_path, "count", "5");
    std::fprintf(stderr, "PROOF client_status=%d\n", client_status);
    feeder.join();
    serving.store(false);
    server.join();

    if (!fed.load()) { std::fprintf(stderr, "PROOF feed_failed\n"); return 1; }
    if (client_status != 0) {
        std::fprintf(stderr, "PROOF client_status=%d\n", client_status);
        return 1;
    }
    const auto expected = expected_ts(0x41U, kPackets);
    const auto actual = read_file(output_path);
    if (actual != expected) {
        std::fprintf(stderr, "PROOF byte_mismatch actual=%zu expected=%zu\n",
                     actual.size(), expected.size());
        return 1;
    }
    if (test_libusb_last_tune_hz() != 473142857U || test_libusb_tune_requests() != 1U) {
        std::fprintf(stderr, "PROOF tune hz=%u requests=%u\n",
                     test_libusb_last_tune_hz(), test_libusb_tune_requests());
        return 1;
    }
    std::fprintf(stderr, "PROOF ts_bytes=%zu hz=%u tune_requests=%u\n",
                 actual.size(), test_libusb_last_tune_hz(), test_libusb_tune_requests());

    const posix::ContextCloseResult closed = owned.close(std::chrono::milliseconds(15000));
    if (!closed.quiesced || closed.error != Error::OK) {
        std::fprintf(stderr, "PROOF close quiesced=%d error=%s\n",
                     closed.quiesced ? 1 : 0, error_string(closed.error));
        return 1;
    }
    std::fprintf(stderr, "PROOF closed quiesced=true error=OK\n");

    // 3. A short no-TS run ends normally with an empty output file.
    {
        const std::string dir2 = make_private_dir();
        assert(!dir2.empty());
        const std::string inst2 = "proofinst0002";
        auto a2 = assemble_siano_context(config, inst2, dir2.c_str(), epoch.value(),
                                         SIANO_RUNTIME_VERSION);
        if (!a2) {
            std::fprintf(stderr, "PROOF noTs assemble_error=%s\n",
                         error_string(a2.error()));
            return 1;
        }
        posix::EnclosureContext& owned2 = *a2.value().context;
        std::atomic<bool> serving2{true};
        std::thread server2([&]() {
            while (serving2.load()) {
                const auto polled = owned2.poll_once(Timeout{50U});
                if (!polled) break;
            }
        });
        const std::string out2 = dir2 + "/empty.ts";
        const int status2 = run_client(dir2, inst2, out2, "time", "1");
        serving2.store(false);
        server2.join();
        const auto bytes2 = read_file(out2);
        const posix::ContextCloseResult closed2 =
            owned2.close(std::chrono::milliseconds(15000));
        if (status2 != 0 || !bytes2.empty() || !closed2.quiesced || closed2.error != Error::OK) {
            std::fprintf(stderr,
                         "PROOF noTs status=%d bytes=%zu quiesced=%d error=%s\n", status2,
                         bytes2.size(), closed2.quiesced ? 1 : 0, error_string(closed2.error));
            return 1;
        }
        std::fprintf(stderr, "PROOF no_ts status=0 bytes=0 quiesced=true error=OK\n");
    }

    // 4. The README form omits --runtime-dir and XDG_RUNTIME_DIR is absent;
    //    the client must select the same fallback root that the daemon uses.
    {
        const std::string dir3 = make_private_dir();
        assert(!dir3.empty());
        const std::string inst3 = "proofinst0003";
        const char* previous_xdg = std::getenv("XDG_RUNTIME_DIR");
        const char* previous_tmp = std::getenv("TMPDIR");
        const std::string saved_xdg = previous_xdg != nullptr ? previous_xdg : "";
        const std::string saved_tmp = previous_tmp != nullptr ? previous_tmp : "";
        const bool had_xdg = previous_xdg != nullptr;
        const bool had_tmp = previous_tmp != nullptr;
        (void)unsetenv("XDG_RUNTIME_DIR");
        if (setenv("TMPDIR", dir3.c_str(), 1) != 0) {
            if (had_xdg) (void)setenv("XDG_RUNTIME_DIR", saved_xdg.c_str(), 1);
            std::fprintf(stderr, "PROOF autoRoot setenv failed\n");
            return 1;
        }
        const auto selected_root = posix::select_publish_runtime_root();
        if (!selected_root) {
            if (had_xdg) (void)setenv("XDG_RUNTIME_DIR", saved_xdg.c_str(), 1);
            if (had_tmp) (void)setenv("TMPDIR", saved_tmp.c_str(), 1);
            else (void)unsetenv("TMPDIR");
            std::fprintf(stderr, "PROOF autoRoot root_error=%s\n",
                         error_string(selected_root.error()));
            return 1;
        }
        const std::string root3(selected_root.value().data());
        auto a3 = assemble_siano_context(config, inst3, root3.c_str(), epoch.value(),
                                         SIANO_RUNTIME_VERSION);
        if (!a3) {
            if (had_xdg) (void)setenv("XDG_RUNTIME_DIR", saved_xdg.c_str(), 1);
            if (had_tmp) (void)setenv("TMPDIR", saved_tmp.c_str(), 1);
            else (void)unsetenv("TMPDIR");
            std::fprintf(stderr, "PROOF autoRoot assemble_error=%s\n",
                         error_string(a3.error()));
            return 1;
        }
        posix::EnclosureContext& owned3 = *a3.value().context;
        std::atomic<bool> serving3{true};
        std::thread server3([&]() {
            while (serving3.load()) {
                const auto polled = owned3.poll_once(Timeout{50U});
                if (!polled) break;
            }
        });
        const std::string out3 = dir3 + "/empty.ts";
        const int status3 = run_client(root3, inst3, out3, "time", "1", false);
        if (had_xdg) (void)setenv("XDG_RUNTIME_DIR", saved_xdg.c_str(), 1);
        if (had_tmp) (void)setenv("TMPDIR", saved_tmp.c_str(), 1);
        else (void)unsetenv("TMPDIR");
        serving3.store(false);
        server3.join();
        const auto bytes3 = read_file(out3);
        const posix::ContextCloseResult closed3 =
            owned3.close(std::chrono::milliseconds(15000));
        if (status3 != 0 || !bytes3.empty() || !closed3.quiesced ||
            closed3.error != Error::OK) {
            std::fprintf(stderr,
                         "PROOF autoRoot status=%d bytes=%zu quiesced=%d error=%s\n",
                         status3, bytes3.size(), closed3.quiesced ? 1 : 0,
                         error_string(closed3.error));
            return 1;
        }
        std::fprintf(stderr, "PROOF auto_root root=%s status=0 bytes=0 quiesced=true error=OK\n",
                     root3.c_str());
    }
    return 0;
}
