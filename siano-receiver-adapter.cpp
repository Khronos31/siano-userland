/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "siano-receiver-adapter.h"

#include <cerrno>
#include <exception>
#include <limits>

namespace siano::capture {
namespace {

using dtv::userland::Error;

ReceiverAdapter::Result result(Error error) noexcept
{
    return ReceiverAdapter::Result::failure(error);
}

}  // namespace

ReceiverAdapter::ReceiverAdapter(libusb_context* usb,
                                 const siano_device_selector& selector,
                                 std::string selector_text, std::string firmware,
                                 ReceiverId receiver, Consumer& consumer,
                                 Clock& clock, std::size_t queue_packets,
                                 int device_fd) noexcept
    : usb_(usb), device_fd_(device_fd), selector_(selector),
      selector_text_(std::move(selector_text)),
      firmware_(std::move(firmware)), receiver_(receiver), consumer_(consumer),
      clock_(clock), queue_packets_(queue_packets)
{
}

void ReceiverAdapter::set_pid_filters(const std::uint16_t* pids,
                                      std::size_t count) noexcept
{
    pid_count_ = 0U;
    if (pids == nullptr) return;
    if (count > pids_.size()) count = pids_.size();
    for (std::size_t index = 0U; index < count; ++index) pids_[index] = pids[index];
    pid_count_ = count;
}

ReceiverAdapter::~ReceiverAdapter() noexcept
{
    if (opened_ && !close(receiver_)) std::terminate();
    else if (initialized_) destroy_device_sync_state(&device_);
}

ReceiverAdapter::Result ReceiverAdapter::configure(
    Engine& engine, ReceiverId receiver, std::uint64_t epoch) noexcept
{
    using namespace dtv::userland::detail;
    const ReceiverCapability capability{receiver, true, false, true,
                                        TuneReadiness::terminal_lock, true};
    auto strict = engine.configure_strict_capabilities(&capability, 1U);
    if (!strict) return strict;
    return engine.configure_stream_contract(StreamContract::v1, epoch);
}

bool ReceiverAdapter::matches(ReceiverId receiver) const noexcept
{
    return receiver == receiver_;
}

bool ReceiverAdapter::supports(ReceiverId receiver, ReceiverSystem system) const noexcept
{
    return matches(receiver) && system == ReceiverSystem::terrestrial;
}

bool ReceiverAdapter::selects_satellite_stream_before_tune() const noexcept { return false; }
std::uint32_t ReceiverAdapter::lock_settle_ms(ReceiverId, ReceiverSystem) const noexcept { return 0U; }

ReceiverAdapter::Result ReceiverAdapter::from_errno(int rc) noexcept
{
    if (rc == -ETIMEDOUT) return result(Error::TIMEOUT);
    if (rc == -ENODEV || rc == -ENXIO) return result(Error::DISCONNECTED);
    if (rc == -EBUSY || rc == -EALREADY) return result(Error::BUSY);
    if (rc == -EINVAL) return result(Error::INVALID_ARGUMENT);
    return result(Error::INTERNAL);
}

ReceiverAdapter::Result ReceiverAdapter::open(ReceiverId receiver) noexcept
{
    if (!matches(receiver)) return result(Error::INVALID_ARGUMENT);
    if (opened_) return result(Error::BUSY);
    if (init_device_state_capture(&device_, usb_, false, false) != 0)
        return result(Error::INTERNAL);
    initialized_ = true;
    int rc = device_fd_ >= 0 ? open_from_fd(&device_, device_fd_)
                             : open_rio(&device_, &selector_, selector_text_.c_str());
    if (rc == 0) rc = start_streaming(&device_);
    if (rc == 0) rc = set_device_mode(&device_,
                                      firmware_.empty() ? nullptr : firmware_.c_str());
    if (rc != 0) {
        std::uint64_t drops = 0U;
        close_device(&device_, &drops);
        initialized_ = false;
        return from_errno(rc);
    }
    opened_ = true;
    return Result::success();
}

ReceiverAdapter::Result ReceiverAdapter::tune(
    ReceiverId, ReceiverSystem, std::uint32_t, std::uint32_t) noexcept
{
    return result(Error::UNSUPPORTED);
}

dtv::userland::Result<dtv::userland::detail::TuneProgramming>
ReceiverAdapter::program_hz(ReceiverId receiver, ReceiverSystem system,
                            std::uint64_t requested_hz,
                            std::uint32_t timeout_ms) noexcept
{
    using namespace dtv::userland;
    using namespace dtv::userland::detail;
    if (!matches(receiver) || system != ReceiverSystem::terrestrial)
        return dtv::userland::Result<TuneProgramming>::failure(Error::UNSUPPORTED);
    if (!opened_) return dtv::userland::Result<TuneProgramming>::failure(Error::NOT_FOUND);
    if (requested_hz == 0U || requested_hz > std::numeric_limits<std::uint32_t>::max())
        return dtv::userland::Result<TuneProgramming>::failure(Error::INVALID_ARGUMENT);
    const int rc = tune_with_timeout(&device_, static_cast<std::uint32_t>(requested_hz), timeout_ms);
    if (rc != 0) return dtv::userland::Result<TuneProgramming>::failure(
        rc == -ETIMEDOUT ? Error::TIMEOUT : Error::INTERNAL);
    if (!pid_filters_sent_) {
        const int pid_rc = configure_pid_filters(&device_, pids_.data(), pid_count_);
        if (pid_rc != 0)
            return dtv::userland::Result<TuneProgramming>::failure(Error::INTERNAL);
        pid_filters_sent_ = true;
    }
    TuneProgramming programming{};
    programming.programmed_hz.present = true;
    programming.programmed_hz.value = requested_hz;
    const std::uint64_t sampled_ms = clock_.monotonic_ms();
    if (sampled_ms != 0U &&
        sampled_ms <= std::numeric_limits<std::uint64_t>::max() / 1000000U) {
        programming.lock.state = wire::MeasureState::valid;
        programming.lock.value = true;
        programming.lock.sampled_at_ns = sampled_ms * 1000000U;
    }
    return dtv::userland::Result<TuneProgramming>::success(programming);
}

dtv::userland::Result<bool> ReceiverAdapter::is_locked(ReceiverId, ReceiverSystem) noexcept
{
    return dtv::userland::Result<bool>::failure(dtv::userland::Error::UNSUPPORTED);
}

ReceiverAdapter::Result ReceiverAdapter::select_satellite_slot(ReceiverId, std::uint8_t,
                                                                std::uint32_t) noexcept
{ return result(Error::UNSUPPORTED); }
ReceiverAdapter::Result ReceiverAdapter::select_satellite_tsid(ReceiverId, std::uint16_t,
                                                                std::uint32_t) noexcept
{ return result(Error::UNSUPPORTED); }

ReceiverAdapter::Result ReceiverAdapter::close(ReceiverId receiver) noexcept
{
    if (!matches(receiver)) return result(Error::INVALID_ARGUMENT);
    if (stream_attached_) return result(Error::BUSY);
    if (!initialized_) return Result::success();
    std::uint64_t drops = 0U;
    close_device(&device_, &drops);
    initialized_ = false;
    opened_ = false;
    return Result::success();
}

ReceiverAdapter::Result ReceiverAdapter::begin_tune_power(
    ReceiverId receiver, ReceiverSystem system, std::uint8_t voltage) noexcept
{
    if (!matches(receiver) || system != ReceiverSystem::terrestrial)
        return result(Error::UNSUPPORTED);
    return voltage == 0U ? Result::success() : result(Error::UNSUPPORTED);
}
ReceiverAdapter::Result ReceiverAdapter::commit_tune_power(ReceiverId receiver) noexcept
{ return matches(receiver) ? Result::success() : result(Error::INVALID_ARGUMENT); }
ReceiverAdapter::Result ReceiverAdapter::rollback_tune_power(ReceiverId receiver) noexcept
{ return matches(receiver) ? Result::success() : result(Error::INVALID_ARGUMENT); }
void ReceiverAdapter::mark_disconnected(ReceiverId) noexcept {}
ReceiverAdapter::Result ReceiverAdapter::shutdown() noexcept
{ return stream_attached_ ? result(Error::BUSY) : Result::success(); }

ReceiverAdapter::Result ReceiverAdapter::start_capture(ReceiverId receiver,
                                                        ReceiverSystem system) noexcept
{
    if (!supports(receiver, system) || !opened_) return result(Error::INVALID_ARGUMENT);
    return Result::success();
}

ReceiverAdapter::Result ReceiverAdapter::stop_capture(ReceiverId receiver,
                                                       ReceiverSystem system) noexcept
{
    if (!supports(receiver, system)) return result(Error::INVALID_ARGUMENT);
    auto stopped = consumer_.stop();
    if (consumer_.quiesced()) stream_attached_ = false;
    if (!stopped && stopped.error() == Error::NOT_FOUND) return Result::success();
    return stopped;
}

ReceiverAdapter::Result ReceiverAdapter::attach(const Attachment& attachment) noexcept
{
    if (!opened_ || stream_attached_) return result(Error::BUSY);
    auto attached = consumer_.start(&device_, attachment, queue_packets_,
                                    dtv::userland::detail::StreamQueuePolicy::evict_oldest);
    if (!attached) return attached;
    stream_attached_ = true;
    return attached;
}

ReceiverAdapter::Result ReceiverAdapter::detach(const Attachment&) noexcept
{
    if (!stream_attached_) return Result::success();
    auto stopped = consumer_.stop();
    if (consumer_.quiesced()) stream_attached_ = false;
    if (!stopped && stopped.error() == Error::NOT_FOUND) return Result::success();
    return stopped;
}

dtv::userland::Result<dtv::userland::detail::StreamCounters>
ReceiverAdapter::stats(const Attachment& attachment) const noexcept
{ return consumer_.stats(attachment); }

dtv::userland::Result<dtv::userland::detail::StreamFinalSnapshot>
ReceiverAdapter::final_snapshot(const Attachment& attachment) const noexcept
{ return consumer_.final_snapshot(attachment); }

dtv::userland::Result<Consumer::ReadResult> ReceiverAdapter::read(
    const Attachment& attachment, dtv::userland::MutableByteView bytes,
    std::chrono::milliseconds timeout) noexcept
{ return consumer_.read(attachment, bytes, timeout); }

ReceiverAdapter::Result ReceiverAdapter::release_final(const Attachment& attachment) noexcept
{ return consumer_.release_final(attachment); }

}  // namespace siano::capture
