/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "siano-l3-family.h"

#include "siano-l3.h"
#include "siano-l3-libusb.h"

#include "px4/card_service.h"
#include "px4/tuner_service.h"

#include <dtv/userland/host_daemon/tuner_service.h>
#include <dtv/userland/host_runtime/family.h>
#include <dtv/userland/host_listener_context/enclosure_context.h>
#include <dtv/userland/json.h>
#include <dtv/userland/wire.h>

#ifdef _WIN32
#include <dtv/userland/host_runtime/posix_ipc.h>
#include "px4/windows_tuner_nonce.h"
#else
#include "px4/posix_ipc.h"
#include "px4/posix_tuner_nonce.h"
#endif

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace siano::runtime {
namespace {

namespace wire = dtv::userland::wire;
using px4::userland::Error;
using px4::userland::Result;
using px4::userland::TunerAttachment;
using px4::userland::TunerHzProgramming;
using px4::userland::TunerStreamCounters;
using px4::userland::TunerStreamFinalSnapshot;
using px4::userland::TunerStreamReadResult;
using px4::userland::TunerStreamTerminal;
using px4::userland::ipc::System;

constexpr std::uint64_t kStreamArmBudgetMs = 5000U;
#ifdef _WIN32
using PlatformTunerNonceSource = px4::userland::ipc::windows::WindowsTunerNonceSource;
#else
using PlatformTunerNonceSource = px4::userland::ipc::posix::PosixTunerNonceSource;
#endif

class Clock final : public px4::userland::TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

class CardBackend final : public px4::userland::CardServiceBackend {
public:
    Result<void> set_power(bool) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> initialize_uart() noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<bool> detect_card() noexcept override
    {
        return Result<bool>::failure(Error::UNSUPPORTED);
    }
};

class CardProtocol final : public px4::userland::CardProtocolSession {
public:
    Result<void> initialize() noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<std::size_t> transmit(dtv::userland::ByteView,
                                 dtv::userland::MutableByteView) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }
    bool initialized() const noexcept override { return false; }
    const px4::userland::CardAtrBytes& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override {}
private:
    px4::userland::CardAtrBytes atr_{};
};

class L3Backend;

class L3Family final : public dtv::userland::host_runtime::EnclosureFamily {
public:
    explicit L3Family(const SianoRuntimeConfig& config,
                      const SianoFacts& facts, unsigned index) noexcept;
    ~L3Family() noexcept override;

    L3Family(const L3Family&) = delete;
    L3Family& operator=(const L3Family&) = delete;

    px4::userland::TunerServiceBackend& tuner_backend() noexcept override;
    px4::userland::CardServiceBackend& card_backend() noexcept override { return card_; }
    px4::userland::CardProtocolSession& card_protocol() noexcept override { return protocol_; }
    px4::userland::TunerServiceTime& time() noexcept override { return clock_; }
    px4::userland::TunerStreamControl* stream_control() noexcept override;

    Result<void> start_device() noexcept;
    SianoFacts facts() const noexcept { return facts_; }
    TunerStreamCounters current_counters() const noexcept;
    bool device_initialized() const noexcept { return owner_ != nullptr; }

private:
    friend class L3Backend;
    Result<void> ensure_lease() noexcept;
    Result<void> detach_stream() noexcept;
    void close_context() noexcept;

    SianoRuntimeConfig config_{};
    SianoFacts facts_{};
    unsigned index_ = 0U;
    Clock clock_{};
    CardBackend card_{};
    CardProtocol protocol_{};
    std::vector<std::uint8_t> firmware_;
    siano_l3_libusb_session* session_ = nullptr;
    siano_l3_adapter* adapter_ = nullptr;
    dtv_l3_context* owner_ = nullptr;
    dtv_l3_lease* lease_ = nullptr;
    dtv_l3_stream* stream_ = nullptr;
    bool running_ = false;
    bool detached_ = false;
    std::uint64_t attachment_id_ = 0U;
    std::uint64_t stream_generation_ = 0U;
    TunerStreamCounters final_counters_{};
    TunerStreamTerminal final_terminal_ = TunerStreamTerminal::none;
    std::unique_ptr<L3Backend> backend_;
};

class L3Backend final : public px4::userland::TunerServiceBackend,
                        public px4::userland::TunerStreamControl {
public:
    explicit L3Backend(L3Family& family) noexcept : family_(family) {}

    std::uint8_t receiver_count() const noexcept override { return 1U; }
    bool receiver_supports(std::uint8_t receiver, System system) const noexcept override
    {
        return receiver == 0U && system == System::ISDB_T;
    }
    px4::userland::TunerStrictProfile strict_profile(
        std::uint8_t receiver, System system) const noexcept override
    {
        if (receiver != 0U || system != System::ISDB_T)
            return px4::userland::TunerStrictProfile{};
        px4::userland::TunerStrictProfile profile{};
        profile.native_hz = true;
        profile.min_hz = 1U;
        profile.max_hz = 0xffffffffULL;
        profile.readiness = dtv::userland::detail::TuneReadiness::terminal_lock;
        return profile;
    }
    bool selects_satellite_stream_before_tune() const noexcept override { return false; }

    Result<void> open_receiver(std::uint8_t receiver) noexcept override
    {
        if (receiver != 0U) return Result<void>::failure(Error::INVALID_ARGUMENT);
        return family_.ensure_lease();
    }
    Result<void> tune_terrestrial(std::uint8_t receiver,
                                  std::uint32_t frequency_khz,
                                  std::uint32_t timeout_ms) noexcept override
    {
        (void)timeout_ms;
        if (receiver != 0U || family_.lease_ == nullptr)
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        std::uint64_t programmed = 0U;
        std::uint64_t revision = 0U;
        if (dtv_l3_lease_tune(family_.lease_,
                              static_cast<std::uint64_t>(frequency_khz) * 1000U,
                              &programmed, &revision) != DTV_L3_OK)
            return Result<void>::failure(Error::USB_IO);
        family_.running_ = true;
        return Result<void>::success();
    }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t,
                                std::uint32_t) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<TunerHzProgramming> tune_terrestrial_hz(std::uint8_t receiver,
                                                   std::uint64_t frequency_hz,
                                                   std::uint32_t) noexcept override
    {
        if (receiver != 0U || family_.lease_ == nullptr)
            return Result<TunerHzProgramming>::failure(Error::INVALID_ARGUMENT);
        std::uint64_t programmed = 0U;
        std::uint64_t revision = 0U;
        if (dtv_l3_lease_tune(family_.lease_, frequency_hz, &programmed,
                              &revision) != DTV_L3_OK)
            return Result<TunerHzProgramming>::failure(Error::USB_IO);
        family_.running_ = true;
        TunerHzProgramming programming{};
        programming.programmed_hz = programmed;
        programming.lock.state = wire::MeasureState::valid;
        programming.lock.value = true;
        programming.ts_synchronized = false;
        return Result<TunerHzProgramming>::success(programming);
    }
    Result<TunerHzProgramming> tune_satellite_hz(std::uint8_t, std::uint64_t,
                                                 std::uint32_t) noexcept override
    {
        return Result<TunerHzProgramming>::failure(Error::UNSUPPORTED);
    }
    Result<bool> is_locked(std::uint8_t receiver, System system) noexcept override
    {
        (void)receiver;
        (void)system;
        return Result<bool>::success(family_.running_);
    }
    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> close_receiver(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> begin_tune_power(std::uint8_t, System,
                                  std::uint8_t lnb_voltage) noexcept override
    {
        return lnb_voltage == 0U ? Result<void>::success()
                                 : Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> commit_tune_power(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> rollback_tune_power(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> set_independent_lnb_power(bool) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    void mark_receiver_disconnected(std::uint8_t) noexcept override {}
    Result<void> shutdown() noexcept override
    {
        return family_.detach_stream();
    }
    Result<void> start_capture(std::uint8_t receiver, System system) noexcept override
    {
        (void)system;
        return receiver == 0U ? Result<void>::success()
                              : Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    Result<void> stop_capture(std::uint8_t receiver, System) noexcept override
    {
        return receiver == 0U ? Result<void>::success()
                              : Result<void>::failure(Error::INVALID_ARGUMENT);
    }

    Result<void> attach(const TunerAttachment& attachment) noexcept override
    {
        if (attachment.receiver != 0U || family_.lease_ == nullptr)
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        if (family_.stream_ != nullptr) {
            const auto detached = family_.detach_stream();
            if (!detached) return detached;
        }
        if (!family_.running_ && dtv_l3_lease_start(family_.lease_) != DTV_L3_OK)
            return Result<void>::failure(Error::USB_IO);
        dtv_l3_stream* stream = nullptr;
        if (dtv_l3_lease_attach(family_.lease_, &stream) != DTV_L3_OK ||
            stream == nullptr)
            return Result<void>::failure(Error::USB_IO);
        family_.stream_ = stream;
        family_.attachment_id_ = attachment.attachment_id;
        family_.detached_ = false;
        family_.stream_generation_ = 0U;
        dtv_l3_stream_status status{};
        if (dtv_l3_stream_status_get(stream, &status) == DTV_L3_OK)
            family_.stream_generation_ = status.stream_generation;
        return Result<void>::success();
    }
    Result<void> detach(const TunerAttachment&) noexcept override
    {
        return family_.detach_stream();
    }
    Result<TunerStreamCounters> stats(const TunerAttachment&) const noexcept override
    {
        return Result<TunerStreamCounters>::success(family_.current_counters());
    }
    Result<TunerStreamFinalSnapshot> final_snapshot(
        const TunerAttachment&) const noexcept override
    {
        TunerStreamFinalSnapshot snapshot{};
        snapshot.counters = family_.final_counters_;
        snapshot.terminal = static_cast<std::uint8_t>(family_.final_terminal_);
        return Result<TunerStreamFinalSnapshot>::success(snapshot);
    }
    Result<void> release_final(const TunerAttachment&) noexcept override
    {
        return Result<void>::success();
    }
    Result<TunerStreamReadResult> read(const TunerAttachment&,
                                       dtv::userland::MutableByteView output,
                                       dtv::userland::Timeout timeout) noexcept override
    {
        TunerStreamReadResult result{};
        if (family_.stream_ == nullptr) {
            result.eof = true;
            result.terminal = TunerStreamTerminal::stopped;
            return Result<TunerStreamReadResult>::success(result);
        }
        size_t bytes = 0U;
        dtv_l3_read_state state = DTV_L3_READ_WAIT;
        (void)timeout;
        const dtv_l3_result read_result = dtv_l3_stream_read_ts_wait(
            family_.stream_, output.data, output.size, &bytes, &state, 1000U);
        if (read_result == DTV_L3_WAIT_TIMEOUT || state == DTV_L3_READ_WAIT) {
            result.timed_out = true;
            return Result<TunerStreamReadResult>::success(result);
        }
        if (read_result == DTV_L3_CLOSING || state == DTV_L3_READ_EOF) {
            result.eof = true;
            result.terminal = TunerStreamTerminal::stopped;
            return Result<TunerStreamReadResult>::success(result);
        }
        if (read_result != DTV_L3_OK)
            return Result<TunerStreamReadResult>::failure(Error::USB_IO);
        result.bytes = bytes;
        return Result<TunerStreamReadResult>::success(result);
    }
    Result<TunerStreamTerminal> terminal(const TunerAttachment&) const noexcept override
    {
        return Result<TunerStreamTerminal>::success(family_.final_terminal_);
    }

private:
    L3Family& family_;
};

L3Family::L3Family(const SianoRuntimeConfig& config, const SianoFacts& facts,
                  unsigned index) noexcept
    : config_(config), facts_(facts), index_(index)
{
    backend_ = std::make_unique<L3Backend>(*this);
}

L3Family::~L3Family() noexcept
{
    (void)detach_stream();
    close_context();
}

px4::userland::TunerServiceBackend& L3Family::tuner_backend() noexcept
{
    return *backend_;
}

px4::userland::TunerStreamControl* L3Family::stream_control() noexcept
{
    return backend_.get();
}

TunerStreamCounters L3Family::current_counters() const noexcept
{
    TunerStreamCounters counters{};
    siano_l3_stats stats{};
    if (adapter_ != nullptr && siano_l3_adapter_stats(adapter_, &stats) == DTV_L3_OK) {
        counters.packets = stats.ts_packets;
        counters.bytes = stats.ts_bytes;
        counters.sync_errors = stats.ts_bad_sync;
        counters.tei_packets = stats.ts_tei_errors;
        counters.continuity_errors = stats.ts_continuity_errors;
    }
    if (stream_ != nullptr) {
        dtv_l3_stream_status status{};
        if (dtv_l3_stream_status_get(stream_, &status) == DTV_L3_OK)
            counters.queue_drops = status.dropped_overflow_packets;
    }
    for (std::size_t index = 0U; index < 5U; ++index)
        counters.availability[index] = wire::MeasureState::valid;
    return counters;
}

Result<void> L3Family::ensure_lease() noexcept
{
    if (owner_ == nullptr || lease_ != nullptr) return Result<void>::success();
    if (dtv_l3_lease_acquire(owner_, 0U, &lease_) != DTV_L3_OK)
        return Result<void>::failure(Error::USB_IO);
    return Result<void>::success();
}

Result<void> L3Family::detach_stream() noexcept
{
    if (stream_ == nullptr) return Result<void>::success();
    final_counters_ = current_counters();
    (void)dtv_l3_stream_stop(stream_, 1000U);
    (void)dtv_l3_stream_release(stream_, DTV_L3_STREAM_RELEASE_DISCARD_UNREAD);
    (void)dtv_l3_stream_destroy(stream_);
    stream_ = nullptr;
    if (lease_ != nullptr) {
        (void)dtv_l3_lease_release(lease_);
        lease_ = nullptr;
    }
    detached_ = true;
    final_terminal_ = TunerStreamTerminal::stopped;
    return Result<void>::success();
}

void L3Family::close_context() noexcept
{
    if (std::getenv("SIANO_L3_DIAGNOSTICS") != nullptr && adapter_ != nullptr) {
        siano_l3_stats stats{};
        if (siano_l3_adapter_stats(adapter_, &stats) == DTV_L3_OK) {
            std::fprintf(stderr,
                         "SIANO_L3_STATS owner_sub=%llu owner_comp=%llu "
                         "in_flight_max=%u in_submits=%llu in_completions=%llu "
                         "ts_packets=%llu ts_tei=%llu ts_cc=%llu\n",
                         (unsigned long long)stats.owner_in_submits,
                         (unsigned long long)stats.owner_in_completions,
                         stats.in_flight_max,
                         (unsigned long long)stats.in_submits,
                         (unsigned long long)stats.in_completions,
                         (unsigned long long)stats.ts_packets,
                         (unsigned long long)stats.ts_tei_errors,
                         (unsigned long long)stats.ts_continuity_errors);
        }
    }
    if (owner_ != nullptr) {
        (void)dtv_l3_close(owner_, 1000U);
        dtv_l3_close_status status{};
        for (unsigned attempt = 0; attempt < 1000U; ++attempt) {
            if (dtv_l3_close_status_get(owner_, &status) != DTV_L3_OK ||
                (status.cleanup_complete && status.all_ops_recovered))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (unsigned attempt = 0; attempt < 1000U; ++attempt) {
            if (dtv_l3_context_release(owner_) != DTV_L3_BUSY) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        owner_ = nullptr;
    }
    if (session_ != nullptr) {
        siano_l3_libusb_session_close(session_);
        session_ = nullptr;
        adapter_ = nullptr;
    }
}

Result<void> L3Family::start_device() noexcept
{
    if (owner_ != nullptr) return Result<void>::success();
    if (!config_.firmware.empty()) {
        std::ifstream file(config_.firmware, std::ios::binary);
        if (!file) return Result<void>::failure(Error::INVALID_ARGUMENT);
        firmware_.assign(std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>());
        if (firmware_.empty()) return Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    siano_l3_firmware firmware{};
    if (!firmware_.empty()) {
        firmware.data = firmware_.data();
        firmware.size = firmware_.size();
    }
    siano_l3_options options{};
    options.boot_mode = SIANO_L3_BOOT_AUTO;
    options.stats_poll_ms = 1000U;
    options.in_flight = 1U;
    options.pid_count = static_cast<std::uint32_t>(config_.pid_count);
    for (std::size_t index = 0U; index < config_.pid_count && index < 64U; ++index)
        options.pids[index] = config_.pids[index];
    const dtv_l3_result opened =
        config_.device_fd >= 0
            ? siano_l3_libusb_session_open_fd(
                  config_.device_fd, firmware_.empty() ? nullptr : &firmware,
                  &options, &session_, nullptr, 0U)
            : siano_l3_libusb_session_open(
                  index_, firmware_.empty() ? nullptr : &firmware, &options,
                  &session_, nullptr, 0U);
    if (opened != DTV_L3_OK) return Result<void>::failure(Error::USB_IO);
    adapter_ = siano_l3_libusb_session_adapter(session_);
    const dtv_l3_usb_provider* usb = siano_l3_adapter_usb_provider(adapter_);
    const dtv_l3_product_adapter* product =
        siano_l3_adapter_product_adapter(adapter_);
    if (usb == nullptr || product == nullptr)
        return Result<void>::failure(Error::INTERNAL);
    dtv_l3_open_result result = dtv_l3_open_ex(usb, product, 1U);
    if (result.result != DTV_L3_ACCEPTED || result.owner == nullptr ||
        result.op == nullptr)
        return Result<void>::failure(Error::USB_IO);
    dtv_l3_op_result op{};
    for (unsigned attempt = 0; attempt < 6000U; ++attempt) {
        if (dtv_l3_op_poll(result.op, &op) != DTV_L3_OK)
            return Result<void>::failure(Error::USB_IO);
        if (op.state == DTV_L3_OP_DONE) break;
        if (op.state == DTV_L3_OP_FAILED)
            return Result<void>::failure(Error::USB_IO);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (op.state != DTV_L3_OP_DONE || op.result != DTV_L3_OK)
        return Result<void>::failure(Error::USB_IO);
    (void)dtv_l3_op_release(result.op);
    owner_ = result.owner;
    if (config_.device_fd >= 0) {
        const siano_l3_port* port = siano_l3_libusb_session_port(session_);
        if (port != nullptr) {
            facts_.vendor_id = port->vendor_id;
            facts_.product_id = port->product_id;
            facts_.bus = port->bus;
            facts_.address = port->address;
            facts_.port_count = port->port_count;
            facts_.has_bus = port->bus != 0U;
            facts_.has_address = port->address != 0U;
            for (unsigned i = 0; i < port->port_count && i < 8U; ++i)
                facts_.ports[i] = port->ports[i];
        }
    }
    return Result<void>::success();
}

Result<SianoFacts> resolve_facts(const SianoRuntimeConfig& config,
                                 unsigned* index_out) noexcept
{
    if (config.device_fd >= 0)
        return Result<SianoFacts>::failure(Error::INVALID_ARGUMENT);
    unsigned index = 0U;
    char error[128] = {0};
    if (siano_l3_libusb_find_selector(&config.selector,
                                      config.selector_text.c_str(), &index,
                                      error, sizeof(error)) != DTV_L3_OK)
        return Result<SianoFacts>::failure(Error::NOT_FOUND);
    siano_l3_port port{};
    if (!siano_l3_libusb_port(index, &port))
        return Result<SianoFacts>::failure(Error::USB_IO);
    SianoFacts facts{};
    facts.vendor_id = port.vendor_id;
    facts.product_id = port.product_id;
    facts.bus = port.bus;
    facts.address = port.address;
    facts.port_count = port.port_count;
    facts.has_bus = port.bus != 0U;
    facts.has_address = port.address != 0U;
    for (unsigned i = 0; i < port.port_count && i < 8U; ++i)
        facts.ports[i] = port.ports[i];
    *index_out = index;
    return Result<SianoFacts>::success(facts);
}

}  // namespace

px4::userland::Result<SianoAssembly> assemble_siano_l3_context(
    const SianoRuntimeConfig& config, const std::string& requested_instance,
    const char* runtime_directory, const dtv::userland::wire::Epoch& epoch,
    const char* product_version, AssemblyDiagnostic* diagnostic) noexcept
{
    namespace identity = dtv::userland::identity;
    if (diagnostic != nullptr) *diagnostic = AssemblyDiagnostic{};
#if !defined(_WIN32)
    if ((runtime_directory == nullptr || runtime_directory[0] == '\0') &&
        config.access == dtv::userland::host_runtime::EndpointAccess::shared_group) {
        if (diagnostic != nullptr)
            diagnostic->reason = AssemblyDiagnosticReason::group_requires_runtime_directory;
        return Result<SianoAssembly>::failure(Error::INVALID_ARGUMENT);
    }
#endif
    SianoFacts facts{};
    unsigned index = 0U;
    bool claimed_before_publish = false;
    std::unique_ptr<L3Family> family;
    if (config.device_fd >= 0) {
        family = std::make_unique<L3Family>(config, SianoFacts{}, 0U);
        const auto claimed = family->start_device();
        if (!claimed) return Result<SianoAssembly>::failure(claimed.error());
        claimed_before_publish = true;
        facts = family->facts();
    } else {
        const auto found = resolve_facts(config, &index);
        if (!found) return Result<SianoAssembly>::failure(found.error());
        facts = found.value();
        family = std::make_unique<L3Family>(config, facts, index);
    }

    std::optional<dtv::userland::identity::ResolvedIdentity> resolved;
    if (facts.has_bus && facts.port_count > 0U) {
        const auto identified = siano_identity(facts);
        if (!identified) return Result<SianoAssembly>::failure(identified.error());
        resolved = identified.value();
    }
    SianoAssembly assembly;
    if (resolved) {
        const auto automatic = identity::auto_instance(*resolved);
        if (!automatic) return Result<SianoAssembly>::failure(automatic.error());
        assembly.lease_identity.assign(automatic.value().bytes.data(),
                                       automatic.value().length);
        if (requested_instance.empty()) {
            assembly.instance = assembly.lease_identity;
        } else {
            const auto canonical = identity::canonicalize_instance(requested_instance);
            if (!canonical) {
                if (diagnostic != nullptr)
                    diagnostic->reason = AssemblyDiagnosticReason::invalid_instance;
                return Result<SianoAssembly>::failure(Error::INVALID_ARGUMENT);
            }
            assembly.instance.assign(canonical.value().bytes.data(),
                                     canonical.value().length);
        }
    } else {
        if (requested_instance.empty()) {
            if (diagnostic != nullptr)
                diagnostic->reason = AssemblyDiagnosticReason::instance_required_with_fd;
            return Result<SianoAssembly>::failure(Error::INVALID_ARGUMENT);
        }
        const auto canonical = identity::canonicalize_instance(requested_instance);
        if (!canonical) {
            if (diagnostic != nullptr)
                diagnostic->reason = AssemblyDiagnosticReason::invalid_instance;
            return Result<SianoAssembly>::failure(Error::INVALID_ARGUMENT);
        }
        assembly.instance.assign(canonical.value().bytes.data(),
                                 canonical.value().length);
        assembly.lease_identity = assembly.instance;
    }
    if (!claimed_before_publish) {
        const auto claimed = family->start_device();
        if (!claimed) return Result<SianoAssembly>::failure(claimed.error());
    }
    const char* selected_root = runtime_directory;
    std::string automatic_root;
    if (selected_root == nullptr) {
        const auto selected = px4::userland::ipc::posix::select_publish_runtime_root(
            "siano-userland", assembly.instance.c_str());
        if (!selected) return Result<SianoAssembly>::failure(selected.error());
        automatic_root = selected.value();
        selected_root = automatic_root.c_str();
    }
    dtv::userland::host_listener_context::ContextConfig context_config{};
    context_config.runtime_directory = selected_root != nullptr ? selected_root : "";
    context_config.product = "siano-userland";
    context_config.instance = assembly.instance;
    context_config.access = config.access;
    dtv::userland::host_runtime::EndpointDiagnostic endpoint_diagnostic{};
    const auto endpoint = dtv::userland::host_runtime::endpoint_path(
        dtv::userland::host_runtime::EndpointConfig{
            context_config.runtime_directory, context_config.product,
            context_config.instance, context_config.access},
        &endpoint_diagnostic);
    if (!endpoint) {
        if (diagnostic != nullptr) {
            diagnostic->reason = AssemblyDiagnosticReason::endpoint_path;
            diagnostic->endpoint = endpoint_diagnostic;
        }
        return Result<SianoAssembly>::failure(endpoint.error());
    }
    assembly.endpoint_path = endpoint.value();
    const auto built = build_siano_control_context(facts, epoch, product_version,
                                                   context_config.canonical_list);
    if (!built) return Result<SianoAssembly>::failure(built.error());
    context_config.server.epoch = epoch;
    context_config.server.producer = context_config.canonical_list.producer;
    context_config.context.epoch = epoch;
    context_config.context.state = wire::ContextState::ready;
    context_config.context.instance.present = true;
    context_config.context.instance.value.length =
        static_cast<std::uint16_t>(assembly.instance.size());
    std::memcpy(context_config.context.instance.value.bytes.data(),
                assembly.instance.data(), assembly.instance.size());
    for (std::size_t i = 0U; i < sizeof(context_config.stream_engine_epoch); ++i)
        context_config.stream_engine_epoch =
            (context_config.stream_engine_epoch << 8U) | epoch[i];
    if (context_config.stream_engine_epoch == 0U)
        context_config.stream_engine_epoch = 1U;
    context_config.stream_arm_budget_ms = kStreamArmBudgetMs;
    auto assembled = dtv::userland::host_listener_context::EnclosureContext::assemble(
        std::move(family),
        std::unique_ptr<dtv::userland::host_runtime::TunerNonceSource>(
            new (std::nothrow) PlatformTunerNonceSource()),
        context_config);
    if (!assembled) return Result<SianoAssembly>::failure(assembled.error());
    assembly.family = nullptr;
    assembly.context = std::move(assembled.value());
    dtv::userland::host_listener_context::ListenerDiagnostic listener_diagnostic{};
    const auto reserved = assembly.context->reserve_namespace(&listener_diagnostic);
    if (!reserved) {
        if (diagnostic != nullptr) {
            diagnostic->reason = AssemblyDiagnosticReason::listener;
            diagnostic->listener = listener_diagnostic;
        }
        return Result<SianoAssembly>::failure(reserved.error());
    }
    const auto published = assembly.context->publish();
    if (!published) return Result<SianoAssembly>::failure(published.error());
    return Result<SianoAssembly>::success(std::move(assembly));
}

}  // namespace siano::runtime
