/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SIANO_RUNTIME_BACKEND_H
#define SIANO_RUNTIME_BACKEND_H

#include "px4/card_service.h"
#include "px4/enclosure_context.h"
#include "px4/tuner_service.h"
#include "siano-receiver-adapter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" {
#include "device-selector.h"
#include "siano-device.h"
#include "siano-os.h"
}

namespace siano::runtime {

// One clock shared by the P service facade and the accepted S adapter so the
// engine and adapter lock samples live in the same monotonic domain.
class SianoClock final : public dtv::userland::detail::ReceiverClock,
                         public px4::userland::TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override;
    void sleep_ms(std::uint32_t milliseconds) noexcept override;
};

// Stateless forwarder from the P service seams to the accepted S adapter.  It
// holds no hardware and no state; it only maps P's legacy facade types onto
// the shared core attachment/counter types.
class SianoRuntimeBackend final : public px4::userland::TunerServiceBackend,
                                  public px4::userland::TunerStreamControl {
public:
    explicit SianoRuntimeBackend(capture::ReceiverAdapter& adapter,
                                 const bool* device_held) noexcept
        : adapter_(adapter), device_held_(device_held)
    {
    }

    std::uint8_t receiver_count() const noexcept override { return 1U; }
    bool receiver_supports(std::uint8_t receiver,
                           px4::userland::ipc::System system) const noexcept override;
    bool selects_satellite_stream_before_tune() const noexcept override { return false; }
    bool requires_terrestrial_lock_settle() const noexcept override { return false; }

    px4::userland::Result<void> open_receiver(std::uint8_t receiver) noexcept override;
    px4::userland::Result<void> tune_terrestrial(
        std::uint8_t receiver, std::uint32_t frequency_khz,
        std::uint32_t timeout_ms) noexcept override;
    px4::userland::Result<void> tune_satellite(
        std::uint8_t receiver, std::uint32_t frequency_khz,
        std::uint32_t timeout_ms) noexcept override;
    px4::userland::Result<px4::userland::TunerHzProgramming> tune_terrestrial_hz(
        std::uint8_t receiver, std::uint64_t frequency_hz,
        std::uint32_t timeout_ms) noexcept override;
    px4::userland::Result<px4::userland::TunerHzProgramming> tune_satellite_hz(
        std::uint8_t receiver, std::uint64_t frequency_hz,
        std::uint32_t timeout_ms) noexcept override;
    px4::userland::TunerStrictProfile strict_profile(
        std::uint8_t receiver, px4::userland::ipc::System system) const noexcept override;
    px4::userland::Result<bool> is_locked(
        std::uint8_t receiver, px4::userland::ipc::System system) noexcept override;
    px4::userland::Result<void> select_satellite_slot(
        std::uint8_t receiver, std::uint8_t slot,
        std::uint32_t timeout_ms) noexcept override;
    px4::userland::Result<void> select_satellite_tsid(
        std::uint8_t receiver, std::uint16_t tsid,
        std::uint32_t timeout_ms) noexcept override;
    px4::userland::Result<void> close_receiver(std::uint8_t receiver) noexcept override;
    px4::userland::Result<void> begin_tune_power(
        std::uint8_t receiver, px4::userland::ipc::System system,
        std::uint8_t lnb_voltage) noexcept override;
    px4::userland::Result<void> commit_tune_power(
        std::uint8_t receiver) noexcept override;
    px4::userland::Result<void> rollback_tune_power(
        std::uint8_t receiver) noexcept override;
    void mark_receiver_disconnected(std::uint8_t receiver) noexcept override;
    px4::userland::Result<void> shutdown() noexcept override;
    px4::userland::Result<void> start_capture(
        std::uint8_t receiver, px4::userland::ipc::System system) noexcept override;
    px4::userland::Result<void> stop_capture(
        std::uint8_t receiver, px4::userland::ipc::System system) noexcept override;

    // TunerStreamControl
    px4::userland::Result<void> attach(
        const px4::userland::TunerAttachment& attachment) noexcept override;
    px4::userland::Result<void> detach(
        const px4::userland::TunerAttachment& attachment) noexcept override;
    px4::userland::Result<px4::userland::TunerStreamCounters> stats(
        const px4::userland::TunerAttachment& attachment) const noexcept override;
    px4::userland::Result<px4::userland::TunerStreamFinalSnapshot> final_snapshot(
        const px4::userland::TunerAttachment& attachment) const noexcept override;
    px4::userland::Result<void> release_final(
        const px4::userland::TunerAttachment& attachment) noexcept override;
    px4::userland::Result<px4::userland::TunerStreamReadResult> read(
        const px4::userland::TunerAttachment& attachment,
        dtv::userland::MutableByteView output,
        dtv::userland::Timeout timeout) noexcept override;

private:
    capture::ReceiverAdapter& adapter_;
    const bool* device_held_ = nullptr;
};

// Card seams for the single-USB-function RIO enclosure.  The RIO has no
// external CARD reader; every card operation is UNSUPPORTED and the reader
// collection is published empty.
class AbsentCardBackend final : public px4::userland::CardServiceBackend {
public:
    px4::userland::Result<void> set_power(bool on) noexcept override;
    px4::userland::Result<void> initialize_uart() noexcept override;
    px4::userland::Result<bool> detect_card() noexcept override;
};

class AbsentCardProtocol final : public px4::userland::CardProtocolSession {
public:
    px4::userland::Result<void> initialize() noexcept override;
    px4::userland::Result<std::size_t> transmit(
        dtv::userland::ByteView apdu,
        dtv::userland::MutableByteView response) noexcept override;
    bool initialized() const noexcept override { return false; }
    const px4::userland::CardAtr& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override {}

private:
    px4::userland::CardAtr atr_{};
};

// Owns the S USB context, clock, accepted Consumer/ReceiverAdapter and the
// P-facing shims.  Declaration order is the reverse of release order so the
// adapter is destroyed before the Consumer it borrows, and the libusb context
// outlives both.
struct SianoRuntimeConfig final {
    // Borrowed when non-null and own_usb is false (the proof loopback).  When
    // own_usb is true the family initializes and exits its own context.
    libusb_context* usb = nullptr;
    bool own_usb = false;
    // >= 0 selects the existing libusb_wrap_sys_device path.  Enumeration is
    // disabled before libusb_init so the daemon accepts a Host API descriptor.
    int device_fd = -1;
    siano_device_selector selector{};
    std::string selector_text;
    std::string firmware;
    std::size_t queue_packets = 65536U;
    std::array<std::uint16_t, 64U> pids{};
    std::size_t pid_count = 0U;
    px4::userland::ipc::posix::EndpointAccess access =
        px4::userland::ipc::posix::EndpointAccess::private_user;
};

class SianoFamily final : public px4::userland::ipc::posix::EnclosureFamily {
public:
    explicit SianoFamily(const SianoRuntimeConfig& config) noexcept;
    ~SianoFamily() noexcept override;

    SianoFamily(const SianoFamily&) = delete;
    SianoFamily& operator=(const SianoFamily&) = delete;

    px4::userland::TunerServiceBackend& tuner_backend() noexcept override
    {
        return backend_;
    }
    px4::userland::CardServiceBackend& card_backend() noexcept override
    {
        return card_backend_;
    }
    px4::userland::CardProtocolSession& card_protocol() noexcept override
    {
        return card_protocol_;
    }
    px4::userland::TunerServiceTime& time() noexcept override { return clock_; }
    px4::userland::TunerStreamControl* stream_control() noexcept override
    {
        return &backend_;
    }

    capture::ReceiverAdapter& adapter() noexcept { return adapter_; }
    capture::Consumer& consumer() noexcept { return consumer_; }
    libusb_context* usb() const noexcept { return usb_; }
    bool stream_attached() const noexcept { return adapter_.stream_attached(); }
    // Claims and initializes the native device before publish so the physical
    // function is held for the whole daemon close, independent of the logical
    // lease open/close.
    px4::userland::Result<void> start_device() noexcept;
    bool device_initialized() const noexcept { return device_started_; }

private:
    libusb_context* usb_ = nullptr;
    bool own_usb_ = false;
    SianoClock clock_{};
    capture::Consumer consumer_{};
    capture::ReceiverAdapter adapter_;
    AbsentCardBackend card_backend_{};
    AbsentCardProtocol card_protocol_{};
    SianoRuntimeBackend backend_;
    bool device_started_ = false;
};

}  // namespace siano::runtime

#endif  // SIANO_RUNTIME_BACKEND_H
