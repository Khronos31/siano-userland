/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SIANO_RECEIVER_ADAPTER_H
#define SIANO_RECEIVER_ADAPTER_H

#include "dtv/userland/detail/receiver_engine.h"
#include "siano-capture-consumer.h"
extern "C" {
#include "siano-device.h"
}

#include <string>

namespace siano::capture {

class ReceiverAdapter final : public dtv::userland::detail::ReceiverBackend,
                              public dtv::userland::detail::ReceiverStreamControl {
public:
    using Engine = dtv::userland::detail::ReceiverEngine;
    using Clock = dtv::userland::detail::ReceiverClock;
    using Result = dtv::userland::Result<void>;
    using ReceiverId = dtv::userland::detail::ReceiverId;
    using ReceiverSystem = dtv::userland::detail::ReceiverSystem;
    using Attachment = dtv::userland::detail::Attachment;

    ReceiverAdapter(libusb_context*, const siano_device_selector&,
                    std::string selector_text, std::string firmware,
                    ReceiverId, Consumer&, Clock&, std::size_t queue_packets) noexcept;
    ~ReceiverAdapter() noexcept override;
    ReceiverAdapter(const ReceiverAdapter&) = delete;
    ReceiverAdapter& operator=(const ReceiverAdapter&) = delete;

    static Result configure(Engine&, ReceiverId, std::uint64_t epoch) noexcept;
    Consumer& consumer() noexcept { return consumer_; }

    bool supports(ReceiverId, ReceiverSystem) const noexcept override;
    bool selects_satellite_stream_before_tune() const noexcept override;
    std::uint32_t lock_settle_ms(ReceiverId, ReceiverSystem) const noexcept override;
    Result open(ReceiverId) noexcept override;
    Result tune(ReceiverId, ReceiverSystem, std::uint32_t,
                std::uint32_t) noexcept override;
    dtv::userland::Result<dtv::userland::detail::TuneProgramming> program_hz(
        ReceiverId, ReceiverSystem, std::uint64_t, std::uint32_t) noexcept override;
    dtv::userland::Result<bool> is_locked(ReceiverId, ReceiverSystem) noexcept override;
    Result select_satellite_slot(ReceiverId, std::uint8_t,
                                 std::uint32_t) noexcept override;
    Result select_satellite_tsid(ReceiverId, std::uint16_t,
                                 std::uint32_t) noexcept override;
    Result close(ReceiverId) noexcept override;
    Result begin_tune_power(ReceiverId, ReceiverSystem, std::uint8_t) noexcept override;
    Result commit_tune_power(ReceiverId) noexcept override;
    Result rollback_tune_power(ReceiverId) noexcept override;
    void mark_disconnected(ReceiverId) noexcept override;
    Result shutdown() noexcept override;
    Result start_capture(ReceiverId, ReceiverSystem) noexcept override;
    Result stop_capture(ReceiverId, ReceiverSystem) noexcept override;

    Result attach(const Attachment&) noexcept override;
    Result detach(const Attachment&) noexcept override;
    dtv::userland::Result<dtv::userland::detail::StreamCounters> stats(
        const Attachment&) const noexcept override;
    dtv::userland::Result<dtv::userland::detail::StreamFinalSnapshot> final_snapshot(
        const Attachment&) const noexcept override;
    dtv::userland::Result<Consumer::ReadResult> read(
        const Attachment&, dtv::userland::MutableByteView,
        std::chrono::milliseconds) noexcept;
    Result release_final(const Attachment&) noexcept;

    siano_device& device() noexcept { return device_; }

private:
    static Result from_errno(int) noexcept;
    bool matches(ReceiverId) const noexcept;

    // The caller owns the libusb context and keeps it alive through close().
    libusb_context* usb_ = nullptr;
    siano_device_selector selector_{};
    std::string selector_text_;
    std::string firmware_;
    ReceiverId receiver_ = 0U;
    Consumer& consumer_;
    Clock& clock_;
    std::size_t queue_packets_ = 0U;
    // The adapter owns the native handle, URBs, queue and lock state in this device.
    siano_device device_{};
    bool initialized_ = false;
    bool opened_ = false;
    bool stream_attached_ = false;
    bool pid_filters_sent_ = false;
};

}  // namespace siano::capture

#endif
