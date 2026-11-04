/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Offline EnclosureContext assembly discriminator; no USB or device access. */
#include "siano-enclosure.h"

#include <dtv/userland/host_daemon/card_service.h>
#include <dtv/userland/host_daemon/tuner_service.h>
#include <dtv/userland/host_listener_context/enclosure_context.h>
#include <dtv/userland/host_runtime/family.h>
#include <dtv/userland/wire.h>

#ifdef _WIN32
#include <dtv/userland/host_runtime/posix_ipc.h>
#include "px4/windows_tuner_nonce.h"
#else
#include "px4/posix_ipc.h"
#include "px4/posix_tuner_nonce.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <thread>

#ifndef _WIN32
#endif

namespace {

using px4::userland::Error;
using px4::userland::Result;

#ifdef _WIN32
using Nonce = px4::userland::ipc::windows::WindowsTunerNonceSource;
#else
using Nonce = px4::userland::ipc::posix::PosixTunerNonceSource;
#endif

Result<void> ok() noexcept { return Result<void>::success(); }
Result<void> unsupported() noexcept { return Result<void>::failure(Error::UNSUPPORTED); }

class Backend final : public px4::userland::TunerServiceBackend {
public:
    std::uint8_t receiver_count() const noexcept override { return 1U; }
    bool receiver_supports(std::uint8_t receiver,
                           px4::userland::ipc::System system) const noexcept override
    {
        return receiver == 0U && system == px4::userland::ipc::System::ISDB_T;
    }
    Result<void> open_receiver(std::uint8_t) noexcept override { return ok(); }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t,
                                  std::uint32_t) noexcept override { return ok(); }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t,
                                std::uint32_t) noexcept override { return unsupported(); }
    Result<bool> is_locked(std::uint8_t,
                           px4::userland::ipc::System) noexcept override
    {
        return Result<bool>::success(true);
    }
    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override { return unsupported(); }
    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override { return unsupported(); }
    Result<void> close_receiver(std::uint8_t) noexcept override { return ok(); }
};

class Stream final : public px4::userland::TunerStreamControl {
public:
    Result<void> attach(const px4::userland::TunerAttachment&) noexcept override { return ok(); }
    Result<void> detach(const px4::userland::TunerAttachment&) noexcept override { return ok(); }
    Result<px4::userland::TunerStreamCounters> stats(
        const px4::userland::TunerAttachment&) const noexcept override
    {
        return Result<px4::userland::TunerStreamCounters>::success({});
    }
    Result<px4::userland::TunerStreamFinalSnapshot> final_snapshot(
        const px4::userland::TunerAttachment&) const noexcept override
    {
        return Result<px4::userland::TunerStreamFinalSnapshot>::success({});
    }
    Result<void> release_final(const px4::userland::TunerAttachment&) noexcept override
    {
        return ok();
    }
};

class Card final : public px4::userland::CardServiceBackend {
public:
    Result<void> set_power(bool) noexcept override { return unsupported(); }
    Result<void> initialize_uart() noexcept override { return unsupported(); }
    Result<bool> detect_card() noexcept override { return Result<bool>::failure(Error::UNSUPPORTED); }
};

class CardProtocol final : public px4::userland::CardProtocolSession {
public:
    Result<void> initialize() noexcept override { return unsupported(); }
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

class Clock final : public px4::userland::TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    void sleep_ms(std::uint32_t ms) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
};

class Family final : public dtv::userland::host_runtime::EnclosureFamily {
public:
    px4::userland::TunerServiceBackend& tuner_backend() noexcept override { return backend_; }
    px4::userland::CardServiceBackend& card_backend() noexcept override { return card_; }
    px4::userland::CardProtocolSession& card_protocol() noexcept override { return protocol_; }
    px4::userland::TunerServiceTime& time() noexcept override { return clock_; }
    px4::userland::TunerStreamControl* stream_control() noexcept override { return &stream_; }
private:
    Backend backend_{};
    Stream stream_{};
    Card card_{};
    CardProtocol protocol_{};
    Clock clock_{};
};

} // namespace


int main()
{
    using siano::runtime::SianoFacts;
    using siano::runtime::build_siano_control_context;
    const char* runtime_env = std::getenv("SIANO_OFFLINE_RUNTIME");
    const auto temp = runtime_env != nullptr
                          ? std::filesystem::path(runtime_env)
                          : std::filesystem::temp_directory_path() / "siano-l3-offline-assembly";
    std::error_code ec;
    std::filesystem::create_directories(temp, ec);
    SianoFacts facts{};
    facts.vendor_id = 0x3275U;
    facts.product_id = 0x0080U;
    facts.bus = 5U;
    facts.address = 4U;
    facts.port_count = 2U;
    facts.ports[0] = 1U;
    facts.ports[1] = 2U;
    facts.has_bus = true;
    facts.has_address = true;
    dtv::userland::wire::Epoch epoch{};
    for (std::size_t i = 0; i < sizeof(epoch); ++i) epoch[i] = static_cast<std::uint8_t>(i + 1U);
    dtv::userland::host_listener_context::ContextConfig config{};
    config.runtime_directory = temp.string();
    config.product = "siano-userland";
    config.instance = "offline1";
    config.access = dtv::userland::host_runtime::EndpointAccess::private_user;
    const auto list = build_siano_control_context(
        facts, epoch, "0.0.0-dev", config.canonical_list);
    if (!list) {
        std::printf("LIST_ERROR=%d\n", static_cast<int>(list.error()));
        return 2;
    }
    config.server.epoch = epoch;
    config.server.producer = config.canonical_list.producer;
    config.context.epoch = epoch;
    config.context.state = dtv::userland::wire::ContextState::ready;
    config.context.instance.present = true;
    config.context.instance.value.length = 8U;
    std::memcpy(config.context.instance.value.bytes.data(), "offline1", 8U);
    config.stream_engine_epoch = 1U;
    config.stream_arm_budget_ms = 5000U;
    auto assembled = dtv::userland::host_listener_context::EnclosureContext::assemble(
        std::make_unique<Family>(),
        std::unique_ptr<dtv::userland::host_runtime::TunerNonceSource>(
            new (std::nothrow) Nonce()),
        config);
    if (!assembled) {
        std::printf("ASSEMBLE_ERROR=%d\n", static_cast<int>(assembled.error()));
        return 1;
    }
    std::printf("ASSEMBLE=OK\n");
    return 0;
}
