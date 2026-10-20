/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "siano-runtime-backend.h"

#include <chrono>
#include <limits>
#include <thread>

#ifdef SIANO_RUNTIME_PROOF_HOOKS
extern "C" void siano_proof_family_destroy_begin();
extern "C" void siano_proof_family_destroy_end();
#endif

namespace siano::runtime {
namespace {

using dtv::userland::Error;
using dtv::userland::Result;
namespace engine = dtv::userland::detail;

engine::ReceiverSystem to_engine(px4::userland::ipc::System system) noexcept
{
    return system == px4::userland::ipc::System::ISDB_S ? engine::ReceiverSystem::satellite
                                                        : engine::ReceiverSystem::terrestrial;
}

engine::Attachment to_engine(const px4::userland::TunerAttachment& attachment) noexcept
{
    engine::Attachment converted{};
    converted.owner_client_id = attachment.owner_client_id;
    converted.lease_id = attachment.lease_id;
    converted.attachment_id = attachment.attachment_id;
    converted.receiver = attachment.receiver;
    converted.system = to_engine(attachment.system);
    converted.token = attachment.nonce;
    converted.evict_oldest = attachment.evict_oldest;
    return converted;
}

px4::userland::TunerStreamCounters to_legacy(const engine::StreamCounters& counters) noexcept
{
    return px4::userland::TunerStreamCounters{counters.packets, counters.bytes,
                                              counters.sync_errors, counters.tei_packets,
                                              counters.continuity_errors,
                                              counters.queue_drops, counters.usb_errors,
                                              counters.empty_intervals};
}

libusb_context* init_own_usb(bool no_device_discovery) noexcept
{
    // The --fd route must not scan udev/usbfs; Termux/Android supplies the
    // descriptor and the daemon wraps it.  Set before libusb_init, as the
    // accepted standalone frontend did.
#if !defined(_WIN32) && defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000107)
    if (no_device_discovery)
        (void)libusb_set_option(NULL, LIBUSB_OPTION_NO_DEVICE_DISCOVERY);
#else
    (void)no_device_discovery;
#endif
    libusb_context* created = nullptr;
    if (libusb_init(&created) != 0) return nullptr;
    return created;
}

}  // namespace

std::uint64_t SianoClock::monotonic_ms() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void SianoClock::sleep_ms(std::uint32_t milliseconds) noexcept
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

bool SianoRuntimeBackend::receiver_supports(std::uint8_t receiver,
                                            px4::userland::ipc::System system) const noexcept
{
    return receiver == 0U && system == px4::userland::ipc::System::ISDB_T;
}

px4::userland::Result<void> SianoRuntimeBackend::open_receiver(
    std::uint8_t receiver) noexcept
{
    // The physical device is claimed at daemon start and held through the
    // whole close, so a logical ACQUIRE reuses it instead of re-opening.
    if (device_held_ != nullptr && *device_held_) {
        return receiver == 0U ? Result<void>::success()
                              : Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    return adapter_.open(receiver);
}

px4::userland::Result<void> SianoRuntimeBackend::tune_terrestrial(
    std::uint8_t, std::uint32_t, std::uint32_t) noexcept
{
    return Result<void>::failure(Error::UNSUPPORTED);
}

px4::userland::Result<void> SianoRuntimeBackend::tune_satellite(
    std::uint8_t, std::uint32_t, std::uint32_t) noexcept
{
    return Result<void>::failure(Error::UNSUPPORTED);
}

px4::userland::Result<px4::userland::TunerHzProgramming>
SianoRuntimeBackend::tune_terrestrial_hz(std::uint8_t receiver, std::uint64_t frequency_hz,
                                         std::uint32_t timeout_ms) noexcept
{
    return adapter_.program_hz(receiver, engine::ReceiverSystem::terrestrial, frequency_hz,
                               timeout_ms);
}

px4::userland::Result<px4::userland::TunerHzProgramming>
SianoRuntimeBackend::tune_satellite_hz(std::uint8_t, std::uint64_t,
                                       std::uint32_t) noexcept
{
    return Result<px4::userland::TunerHzProgramming>::failure(Error::UNSUPPORTED);
}

px4::userland::TunerStrictProfile SianoRuntimeBackend::strict_profile(
    std::uint8_t receiver, px4::userland::ipc::System system) const noexcept
{
    if (receiver != 0U || system != px4::userland::ipc::System::ISDB_T)
        return px4::userland::TunerStrictProfile{};
    px4::userland::TunerStrictProfile profile{};
    profile.native_hz = true;
    profile.min_hz = 1U;
    profile.max_hz = std::numeric_limits<std::uint32_t>::max();
    profile.readiness = engine::TuneReadiness::terminal_lock;
    return profile;
}

px4::userland::Result<bool> SianoRuntimeBackend::is_locked(
    std::uint8_t receiver, px4::userland::ipc::System system) noexcept
{
    return adapter_.is_locked(receiver, to_engine(system));
}

px4::userland::Result<void> SianoRuntimeBackend::select_satellite_slot(
    std::uint8_t, std::uint8_t, std::uint32_t) noexcept
{
    return Result<void>::failure(Error::UNSUPPORTED);
}

px4::userland::Result<void> SianoRuntimeBackend::select_satellite_tsid(
    std::uint8_t, std::uint16_t, std::uint32_t) noexcept
{
    return Result<void>::failure(Error::UNSUPPORTED);
}

px4::userland::Result<void> SianoRuntimeBackend::close_receiver(
    std::uint8_t receiver) noexcept
{
    // A logical RELEASE never closes the physical device; the family closes it
    // once, after the callback/Consumer settles at whole-context cleanup.
    if (device_held_ != nullptr && *device_held_) {
        return receiver == 0U ? Result<void>::success()
                              : Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    return adapter_.close(receiver);
}

px4::userland::Result<void> SianoRuntimeBackend::begin_tune_power(
    std::uint8_t receiver, px4::userland::ipc::System system,
    std::uint8_t lnb_voltage) noexcept
{
    return adapter_.begin_tune_power(receiver, to_engine(system), lnb_voltage);
}

px4::userland::Result<void> SianoRuntimeBackend::commit_tune_power(
    std::uint8_t receiver) noexcept
{
    return adapter_.commit_tune_power(receiver);
}

px4::userland::Result<void> SianoRuntimeBackend::rollback_tune_power(
    std::uint8_t receiver) noexcept
{
    return adapter_.rollback_tune_power(receiver);
}

void SianoRuntimeBackend::mark_receiver_disconnected(std::uint8_t receiver) noexcept
{
    adapter_.mark_disconnected(receiver);
}

px4::userland::Result<void> SianoRuntimeBackend::shutdown() noexcept
{
    return adapter_.shutdown();
}

px4::userland::Result<void> SianoRuntimeBackend::start_capture(
    std::uint8_t receiver, px4::userland::ipc::System system) noexcept
{
    return adapter_.start_capture(receiver, to_engine(system));
}

px4::userland::Result<void> SianoRuntimeBackend::stop_capture(
    std::uint8_t receiver, px4::userland::ipc::System system) noexcept
{
    return adapter_.stop_capture(receiver, to_engine(system));
}

px4::userland::Result<void> SianoRuntimeBackend::attach(
    const px4::userland::TunerAttachment& attachment) noexcept
{
    return adapter_.attach(to_engine(attachment));
}

px4::userland::Result<void> SianoRuntimeBackend::detach(
    const px4::userland::TunerAttachment& attachment) noexcept
{
    return adapter_.detach(to_engine(attachment));
}

px4::userland::Result<px4::userland::TunerStreamCounters> SianoRuntimeBackend::stats(
    const px4::userland::TunerAttachment& attachment) const noexcept
{
    auto counters = adapter_.stats(to_engine(attachment));
    if (!counters) return Result<px4::userland::TunerStreamCounters>::failure(counters.error());
    return Result<px4::userland::TunerStreamCounters>::success(to_legacy(counters.value()));
}

px4::userland::Result<px4::userland::TunerStreamFinalSnapshot>
SianoRuntimeBackend::final_snapshot(
    const px4::userland::TunerAttachment& attachment) const noexcept
{
    auto snapshot = adapter_.final_snapshot(to_engine(attachment));
    if (!snapshot) {
        return Result<px4::userland::TunerStreamFinalSnapshot>::failure(snapshot.error());
    }
    px4::userland::TunerStreamFinalSnapshot legacy{};
    legacy.counters = to_legacy(snapshot.value().counters);
    legacy.terminal = snapshot.value().terminal;
    return Result<px4::userland::TunerStreamFinalSnapshot>::success(legacy);
}

px4::userland::Result<void> SianoRuntimeBackend::release_final(
    const px4::userland::TunerAttachment& attachment) noexcept
{
    return adapter_.release_final(to_engine(attachment));
}

px4::userland::Result<px4::userland::TunerStreamReadResult> SianoRuntimeBackend::read(
    const px4::userland::TunerAttachment& attachment, dtv::userland::MutableByteView output,
    dtv::userland::Timeout timeout) noexcept
{
    auto read = adapter_.read(to_engine(attachment), output,
                              std::chrono::milliseconds(timeout.milliseconds));
    if (!read) return Result<px4::userland::TunerStreamReadResult>::failure(read.error());
    px4::userland::TunerStreamReadResult legacy{};
    legacy.bytes = read.value().bytes;
    legacy.eof = read.value().eof;
    legacy.timed_out = read.value().timed_out;
    legacy.terminal = static_cast<px4::userland::TunerStreamTerminal>(
        static_cast<std::uint8_t>(read.value().terminal));
    return Result<px4::userland::TunerStreamReadResult>::success(legacy);
}

px4::userland::Result<void> AbsentCardBackend::set_power(bool) noexcept
{
    return Result<void>::failure(Error::UNSUPPORTED);
}

px4::userland::Result<void> AbsentCardBackend::initialize_uart() noexcept
{
    return Result<void>::failure(Error::UNSUPPORTED);
}

px4::userland::Result<bool> AbsentCardBackend::detect_card() noexcept
{
    return Result<bool>::failure(Error::UNSUPPORTED);
}

px4::userland::Result<void> AbsentCardProtocol::initialize() noexcept
{
    return Result<void>::failure(Error::UNSUPPORTED);
}

px4::userland::Result<std::size_t> AbsentCardProtocol::transmit(
    dtv::userland::ByteView, dtv::userland::MutableByteView) noexcept
{
    return Result<std::size_t>::failure(Error::UNSUPPORTED);
}

SianoFamily::SianoFamily(const SianoRuntimeConfig& config) noexcept
    : usb_(config.own_usb ? init_own_usb(config.device_fd >= 0) : config.usb),
      own_usb_(config.own_usb),
      adapter_(usb_, config.selector, config.selector_text, config.firmware, 0U, consumer_,
               clock_, config.queue_packets, config.device_fd),
      backend_(adapter_, &device_started_)
{
    adapter_.set_pid_filters(config.pids.data(), config.pid_count);
}

SianoFamily::~SianoFamily() noexcept
{
#ifdef SIANO_RUNTIME_PROOF_HOOKS
    siano_proof_family_destroy_begin();
#endif
    // The accepted adapter owns the native handle, URBs, queue and lock state.
    // Wait for the real callback/Consumer to settle before any owned object is
    // destroyed, then close the device.  The P cleanup calls this destructor
    // from its own cleanup thread, so a held callback blocks here (retaining
    // the graph) and the outer Context.close deadline reports unquiesced.
    (void)adapter_.detach(dtv::userland::detail::Attachment{});
    (void)adapter_.close(0U);
    if (own_usb_ && usb_ != nullptr) libusb_exit(usb_);
#ifdef SIANO_RUNTIME_PROOF_HOOKS
    siano_proof_family_destroy_end();
#endif
}

px4::userland::Result<void> SianoFamily::start_device() noexcept
{
    if (device_started_) return Result<void>::success();
    const auto opened = adapter_.open(0U);
    if (!opened) return opened;
    // The physical claim is held through the whole daemon close.  The logical
    // lease ACQUIRE/RELEASE reuses this open; adapter.open() reports BUSY if it
    // is called again while held.
    device_started_ = true;
    return Result<void>::success();
}

}  // namespace siano::runtime
