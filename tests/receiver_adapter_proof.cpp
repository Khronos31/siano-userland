/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "siano-receiver-adapter.h"

extern "C" {
#include "output-writer.h"
#include "protocol.h"
}

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

extern "C" int test_libusb_deliver(struct libusb_transfer*, const std::uint8_t*,
                                    std::size_t);
extern "C" int test_libusb_pending(struct libusb_transfer*);
extern "C" unsigned test_libusb_control_requests(void);
extern "C" unsigned test_libusb_tune_requests(void);
extern "C" unsigned test_libusb_pid_requests(void);
extern "C" std::uint32_t test_libusb_last_tune_hz(void);
extern "C" void test_libusb_suppress_tune_response(bool);
extern "C" void test_libusb_fail_next_bulk(int);
extern "C" int test_libusb_fail_stream(int);
extern "C" int test_libusb_deliver_control_to(struct libusb_transfer*, std::uint16_t);

namespace {

using namespace dtv::userland;
using namespace dtv::userland::detail;
using namespace siano::capture;

class Tokens final : public ReceiverTokenSource {
public:
    Result<ReceiverToken> generate() noexcept override
    {
        ReceiverToken token{};
        token[0] = ++next;
        return Result<ReceiverToken>::success(token);
    }
    std::uint8_t next = 0U;
};

class Ids final : public ReceiverIdSource {
public:
    Result<std::uint64_t> next() noexcept override
    { return Result<std::uint64_t>::success(++value); }
    std::uint64_t value = 40U;
};

class Clock final : public ReceiverClock {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return static_cast<std::uint64_t>(now.tv_sec) * 1000U +
               static_cast<std::uint64_t>(now.tv_nsec / 1000000L);
    }
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
};

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
    for (unsigned i = 0U; i < 2000U; ++i) {
        if (test_libusb_pending(transfer)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool wait_packets(Consumer& consumer, const Attachment& attachment,
                  std::uint64_t packets)
{
    for (unsigned i = 0U; i < 2000U; ++i) {
        auto stats = consumer.stats(attachment);
        if (stats && stats.value().packets >= packets) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

int main()
{
    constexpr ReceiverId receiver = 7U;
    constexpr std::uint64_t client = 19U;
    Clock clock;
    Tokens tokens;
    Ids leases;
    Ids attachments;
    Consumer consumer;
    siano_device_selector selector{};
    assert(siano_parse_device_selector("0", &selector) == 0);
    ReceiverAdapter backend(nullptr, selector, "0", {}, receiver, consumer, clock, 2U);
    test_libusb_fail_next_bulk(LIBUSB_ERROR_NO_DEVICE);
    const auto unavailable = backend.open(receiver);
    assert(!unavailable && unavailable.error() == Error::DISCONNECTED);
    {
        auto failure_consumer = std::make_unique<Consumer>();
        auto failure_backend = std::make_unique<ReceiverAdapter>(
            nullptr, selector, "0", std::string{}, receiver,
            *failure_consumer, clock, 2U);
        assert(failure_backend->open(receiver));
        assert(test_libusb_fail_stream(LIBUSB_TRANSFER_ERROR) == 0);
        for (unsigned attempt = 0U; attempt < 2000U; ++attempt) {
            if (siano_stream_state_is_stopping(&failure_backend->device().state)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(siano_stream_state_error(&failure_backend->device().state) ==
               LIBUSB_ERROR_NO_DEVICE);
        assert(failure_backend->close(receiver));
    }
    const std::array<ReceiverId, 1> receivers{receiver};
    ReceiverEngine engine(backend, receivers.data(), receivers.size(), tokens, clock,
                          &leases, &attachments, &backend);
    assert(ReceiverAdapter::configure(engine, receiver, 101U));

    auto lease = engine.acquire(client, receiver);
    assert(lease);
    HzTuneRequest tune{};
    tune.lease_id = lease.value().lease_id;
    tune.system = ReceiverSystem::terrestrial;
    tune.requested_hz = 473142857U;
    tune.timeout_ms = 2000U;
    HzTuneRequest unsupported = tune;
    unsupported.system = ReceiverSystem::satellite;
    const unsigned before_unsupported = test_libusb_tune_requests();
    auto rejected = engine.try_admit_tune(client, unsupported, clock.monotonic_ms());
    assert(!rejected && rejected.error() == Error::INVALID_ARGUMENT);
    unsupported = tune;
    unsupported.lnb_voltage = 15U;
    auto power_rejected = engine.try_admit_tune(client, unsupported, clock.monotonic_ms());
    assert(power_rejected && !engine.execute_admitted_tune(power_rejected.value()));
    assert(test_libusb_tune_requests() == before_unsupported);
    unsupported = tune;
    unsupported.requested_hz = static_cast<std::uint64_t>(UINT32_MAX) + 1U;
    auto range_rejected = engine.try_admit_tune(client, unsupported, clock.monotonic_ms());
    assert(range_rejected && !engine.execute_admitted_tune(range_rejected.value()));
    assert(test_libusb_tune_requests() == before_unsupported);

    const unsigned controls_before = test_libusb_control_requests();
    const unsigned pids_before = test_libusb_pid_requests();
    auto admitted = engine.try_admit_tune(client, tune, clock.monotonic_ms());
    if (!admitted) {
        std::fprintf(stderr, "S_ENGINE admission_error=%s\n", error_string(admitted.error()));
        return 1;
    }
    auto tuned = engine.execute_admitted_tune(admitted.value());
    if (!tuned) {
        std::fprintf(stderr, "S_ENGINE tune_error=%s\n", error_string(tuned.error()));
        return 1;
    }
    assert(test_libusb_last_tune_hz() == tune.requested_hz);
    assert(test_libusb_tune_requests() == 1U);
    assert(test_libusb_control_requests() >= controls_before + 2U);
    assert(test_libusb_pid_requests() == pids_before + 1U);
    auto tuned_status = engine.status();
    assert(tuned_status);
    const auto& lock = tuned_status.value().tune_metadata[0].current.lock;
    assert(lock.state == wire::MeasureState::valid && lock.value && lock.sampled_at_ns != 0U);
    std::fprintf(stderr, "S_ENGINE tune_hz=%u tune_requests=%u catchall_pid_requests=%u lock_sample_ns=%llu\n",
                 test_libusb_last_tune_hz(), test_libusb_tune_requests(),
                 test_libusb_pid_requests() - pids_before,
                 static_cast<unsigned long long>(lock.sampled_at_ns));

    auto status = engine.status();
    assert(status && status.value().tune_metadata[0].current_revision != 0U);
    const std::uint64_t revision = status.value().tune_metadata[0].current_revision;
    auto started = engine.start_stream_v1(client, lease.value().lease_id, revision,
                                          clock.monotonic_ms(), 5000U);
    assert(started);
    auto reserved = engine.reserve_attach_v1(client, lease.value().lease_id,
        started.value().epoch, started.value().stream_id, started.value().token,
        clock.monotonic_ms());
    assert(reserved && engine.prepare_attach_v1(reserved.value()));
    assert(engine.commit_attach_v1(reserved.value()));
    const Attachment attachment{client, lease.value().lease_id,
                                reserved.value().reservation, receiver,
                                ReceiverSystem::terrestrial, reserved.value().token};
    assert(attachment.owner_client_id == client);

    for (std::size_t i = 0U; i < MAX_URBS; ++i) {
        assert(wait_transfer_pending(backend.device().transfers[i]));
        assert(test_libusb_deliver_control_to(
            backend.device().transfers[i], MSG_SMS_SIGNAL_DETECTED_IND) == 0);
        assert(wait_transfer_pending(backend.device().transfers[i]));
    }

    assert(deliver_ts(backend.device(), 0x41U, 3U));
    assert(wait_packets(backend.consumer(), attachment, 3U));
    std::array<std::uint8_t, 2U * SIANO_OUTPUT_PACKET> output{};
    auto read = backend.read(attachment, MutableByteView{output.data(), output.size()},
                             std::chrono::milliseconds(1000));
    assert(read && read.value().bytes == output.size());
    assert(output[0] == 0x47U && output[2] == 0x42U);
    assert(output[SIANO_OUTPUT_PACKET] == 0x47U && output[SIANO_OUTPUT_PACKET + 2] == 0x43U);
    auto counters = backend.stats(attachment);
    assert(counters && counters.value().packets == 3U);
    std::fprintf(stderr,
                 "S_ENGINE stream_a packets=%llu read=2 retained_markers=%u,%u terminal=active\n",
                 static_cast<unsigned long long>(counters.value().packets),
                 output[2], output[SIANO_OUTPUT_PACKET + 2]);

    auto stopped = engine.stop_stream_v1(client, lease.value().lease_id,
                                          started.value().stream_id);
    assert(stopped && !stopped.value().quiesced);
    assert(engine.run_stream_cleanup_v1(reserved.value()));
    auto final = engine.stream_last_terminal_v1(client, lease.value().lease_id);
    assert(final && final.value().quiesced);
    assert(backend.release_final(attachment));

    auto started_b = engine.start_stream_v1(client, lease.value().lease_id, revision,
                                            clock.monotonic_ms(), 5000U);
    assert(started_b);
    auto reserved_b = engine.reserve_attach_v1(client, lease.value().lease_id,
        started_b.value().epoch, started_b.value().stream_id, started_b.value().token,
        clock.monotonic_ms());
    assert(reserved_b && engine.prepare_attach_v1(reserved_b.value()));
    assert(engine.commit_attach_v1(reserved_b.value()));
    Attachment attachment_b{client, lease.value().lease_id,
                            reserved_b.value().reservation, receiver,
                            ReceiverSystem::terrestrial, reserved_b.value().token};
    for (std::size_t i = 0U; i < MAX_URBS; ++i) {
        assert(wait_transfer_pending(backend.device().transfers[i]));
        assert(test_libusb_deliver_control_to(
            backend.device().transfers[i], MSG_SMS_SIGNAL_DETECTED_IND) == 0);
        assert(wait_transfer_pending(backend.device().transfers[i]));
    }
    assert(deliver_ts(backend.device(), 0x52U, 1U));
    assert(wait_packets(backend.consumer(), attachment_b, 1U));
    std::array<std::uint8_t, SIANO_OUTPUT_PACKET> output_b{};
    auto read_b = backend.read(attachment_b,
        MutableByteView{output_b.data(), output_b.size()}, std::chrono::milliseconds(1000));
    assert(read_b && read_b.value().bytes == output_b.size() && output_b[2] == 0x52U);
    std::fprintf(stderr, "S_ENGINE stream_b read_bytes=%zu marker=%u\n",
                 read_b.value().bytes, output_b[2]);
    auto stopped_b = engine.stop_stream_v1(client, lease.value().lease_id,
                                           started_b.value().stream_id);
    assert(stopped_b && engine.run_stream_cleanup_v1(reserved_b.value()));
    assert(backend.release_final(attachment_b));

    test_libusb_suppress_tune_response(true);
    HzTuneRequest deadline_tune = tune;
    deadline_tune.timeout_ms = 80U;
    auto deadline_ticket = engine.try_admit_tune(client, deadline_tune,
                                                  clock.monotonic_ms());
    assert(deadline_ticket);
    const auto before = clock.monotonic_ms();
    auto deadline_result = engine.execute_admitted_tune(deadline_ticket.value());
    const auto elapsed = clock.monotonic_ms() - before;
    test_libusb_suppress_tune_response(false);
    assert(!deadline_result && deadline_result.error() == Error::TIMEOUT);
    assert(elapsed < 500U);
    std::fprintf(stderr, "S_ENGINE tune_deadline_ms=%u elapsed_ms=%llu result=TIMEOUT\n",
                 deadline_tune.timeout_ms, static_cast<unsigned long long>(elapsed));
    assert(engine.release(client, lease.value().lease_id));
    assert(engine.shutdown());
    return 0;
}
