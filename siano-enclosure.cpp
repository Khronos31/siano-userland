/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "siano-enclosure.h"

extern "C" {
#include "siano-device.h"
}

#ifdef _WIN32
#include "px4/windows_tuner_nonce.h"
#else
#include "px4/posix_tuner_nonce.h"
#endif

#include <array>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <utility>

namespace siano::runtime {
namespace {

namespace wire = dtv::userland::wire;
using px4::userland::Result;
using px4::userland::Error;
#ifdef _WIN32
using PlatformTunerNonceSource = px4::userland::ipc::windows::WindowsTunerNonceSource;
#else
using PlatformTunerNonceSource = px4::userland::ipc::posix::PosixTunerNonceSource;
#endif

template <std::size_t N>
bool copy_text(wire::WString<N>& out, const char* value) noexcept
{
    if (value == nullptr) return false;
    const std::size_t size = std::strlen(value);
    if (size > N) return false;
    out.length = static_cast<std::uint16_t>(size);
    for (std::size_t i = 0U; i < size; ++i) out.bytes[i] = value[i];
    return true;
}

bool copy_numbers(
    dtv::userland::wire::WString<dtv::userland::detail::kMaxUsbPathBytes>& out,
    const SianoFacts& facts) noexcept
{
    if (!facts.has_bus || facts.port_count == 0U) return false;
    char path[64]{};
    int length = std::snprintf(path, sizeof(path), "%u", static_cast<unsigned int>(facts.bus));
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(path)) return false;
    for (std::size_t port = 0U; port < facts.port_count; ++port) {
        const int written = std::snprintf(path + length, sizeof(path) -
            static_cast<std::size_t>(length), port == 0U ? "-%u" : ".%u",
            static_cast<unsigned int>(facts.ports[port]));
        if (written <= 0 ||
            static_cast<std::size_t>(written) >= sizeof(path) - static_cast<std::size_t>(length))
            return false;
        length += written;
    }
    if (static_cast<std::size_t>(length) > out.bytes.size()) return false;
    out.length = static_cast<std::uint16_t>(length);
    for (int c = 0; c < length; ++c)
        out.bytes[static_cast<std::size_t>(c)] = path[c];
    return true;
}

}  // namespace

px4::userland::Result<SianoFacts> resolve_rio_facts(
    libusb_context* usb, const siano_device_selector& selector,
    const std::string& selector_text) noexcept
{
    struct siano_rio_facts raw {};
    const int rc = locate_rio(usb, &selector, selector_text.c_str(), &raw);
    if (rc != 0) {
        const Error error = rc == -EINVAL ? Error::INVALID_ARGUMENT
                          : rc == -ENODEV ? Error::NOT_FOUND
                                          : Error::USB_IO;
        return Result<SianoFacts>::failure(error);
    }
    SianoFacts facts{};
    facts.vendor_id = raw.vendor_id;
    facts.product_id = raw.product_id;
    facts.bus = raw.bus;
    facts.address = raw.address;
    facts.port_count = raw.port_count;
    facts.has_bus = raw.has_bus;
    facts.has_address = raw.has_address;
    for (std::size_t i = 0U; i < raw.port_count && i < 8U; ++i) facts.ports[i] = raw.ports[i];
    return Result<SianoFacts>::success(facts);
}

px4::userland::Result<dtv::userland::identity::ResolvedIdentity> siano_identity(
    const SianoFacts& facts) noexcept
{
    namespace identity = dtv::userland::identity;
    if (!facts.has_bus || facts.port_count == 0U)
        return Result<identity::ResolvedIdentity>::failure(Error::INVALID_ARGUMENT);
    std::array<char, 96U> selector_text{};
    int length = std::snprintf(selector_text.data(), selector_text.size(), "usb:%u",
                               static_cast<unsigned int>(facts.bus));
    if (length <= 0 || static_cast<std::size_t>(length) >= selector_text.size())
        return Result<identity::ResolvedIdentity>::failure(Error::INVALID_ARGUMENT);
    for (std::size_t port = 0U; port < facts.port_count; ++port) {
        const int written = std::snprintf(selector_text.data() + length,
            selector_text.size() - static_cast<std::size_t>(length),
            port == 0U ? "-%u" : ".%u", static_cast<unsigned int>(facts.ports[port]));
        if (written <= 0 || static_cast<std::size_t>(written) >=
                                selector_text.size() - static_cast<std::size_t>(length))
            return Result<identity::ResolvedIdentity>::failure(Error::INVALID_ARGUMENT);
        length += written;
    }
    const auto selector = identity::parse_device_selector(
        std::string_view(selector_text.data(), static_cast<std::size_t>(length)));
    if (!selector) return Result<identity::ResolvedIdentity>::failure(selector.error());
    return identity::resolve_selector(identity::Product::siano, selector.value());
}

dtv::userland::Result<void> build_siano_control_context(
    const SianoFacts& facts, const std::string& instance,
    const dtv::userland::wire::Epoch& epoch, const char* product_version,
    const px4::userland::TunerService& tuner,
    px4::userland::ipc::posix::ControlContext& out) noexcept
{
    if (product_version == nullptr || tuner.receiver_count() < 1U)
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    out = px4::userland::ipc::posix::ControlContext{};
    out.epoch = epoch;
    if (!instance.empty()) {
        out.instance.present = copy_text(out.instance.value, instance.c_str());
        if (!out.instance.present) return Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    wire::ListResponse& list = out.list;
    list.epoch = epoch;
    if (!copy_text(list.producer.product, "siano-userland") ||
        !copy_text(list.producer.version, product_version))
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    list.topology.state = wire::TopologyState::complete;
    list.topology.expected_function_count.present = true;
    list.topology.expected_function_count.value = 1U;
    list.enclosure.present = true;
    wire::Enclosure& enclosure = list.enclosure.value;
    enclosure.id = 0U;
    // The Siano model id is not in the adopted model catalog, so the model is
    // omitted rather than invented.
    enclosure.identity.serial_state = wire::IdentitySerialState::unknown;
    enclosure.identity.uniqueness = wire::Uniqueness::unknown;
    enclosure.limits.max_concurrent_streams.present = true;
    enclosure.limits.max_concurrent_streams.value = 1U;

    list.functions.count = 1U;
    wire::UsbFunction& function = list.functions.items[0];
    function.id = 0U;
    function.vid = facts.vendor_id;
    function.pid = facts.product_id;
    function.mode = wire::Mode::unknown;
    function.serial_state = wire::SerialState::not_queried;
    // The model is omitted (not in the adopted catalog); the codec requires a
    // non-none reason when a function carries no model.
    function.identification_reason = wire::IdentificationReason::model_unconfirmed;
    if (facts.has_bus) {
        function.location.bus.present = true;
        function.location.bus.value = facts.bus;
    }
    if (facts.has_address) {
        function.location.address.present = true;
        function.location.address.value = facts.address;
    }
    if (facts.has_bus && facts.port_count > 0U) {
        if (!copy_numbers(function.location.usb_path.value, facts))
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        function.location.usb_path.present = true;
    }
    function.present = true;

    list.receivers.present = true;
    list.receivers.value.count = 1U;
    out.execution.receiver_count = 1U;
    wire::ReceiverDescriptor& receiver = list.receivers.value.items[0];
    receiver.id = 0U;
    receiver.systems.mask = 1U;
    receiver.binding.present = true;
    receiver.binding.value.function_id = 0U;
    receiver.binding.value.local_id = 0U;
    receiver.capabilities.ts_scope = wire::TsScope::multiplex;
    out.receiver_index[0] = 0U;
    out.execution.receiver_lane[0] =
        px4::userland::ipc::posix::ControlWorkerLane::tuner_dev1;

    // No external CARD reader exists in the RIO enclosure.
    list.readers.present = true;
    list.readers.value.count = 0U;

    if (wire::validate_list_response(list) != Error::OK)
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    return Result<void>::success();
}

px4::userland::Result<SianoAssembly> assemble_siano_context(
    const SianoRuntimeConfig& config, const std::string& requested_instance,
    const char* runtime_directory, const dtv::userland::wire::Epoch& epoch,
    const char* product_version) noexcept
{
    namespace identity = dtv::userland::identity;
    using namespace px4::userland::ipc::posix;
    std::unique_ptr<SianoFamily> family(new (std::nothrow) SianoFamily(config));
    if (!family) return Result<SianoAssembly>::failure(Error::INTERNAL);

    SianoFacts facts{};
    std::optional<dtv::userland::identity::ResolvedIdentity> resolved;
    bool claimed_before_publish = false;
    if (config.device_fd >= 0) {
        // No enumeration with --fd: wrap the supplied descriptor first, then
        // read the read-only facts from the wrapped handle.
        const auto claimed = family->start_device();
        if (!claimed) {
            std::fprintf(stderr, "assemble: claim %s\n",
                         dtv::userland::error_string(claimed.error()));
            return Result<SianoAssembly>::failure(claimed.error());
        }
        claimed_before_publish = true;
        struct siano_rio_facts raw {};
        const int rc = rio_facts_from_handle(&family->adapter().device(), &raw);
        if (rc != 0) return Result<SianoAssembly>::failure(Error::USB_IO);
        facts.vendor_id = raw.vendor_id;
        facts.product_id = raw.product_id;
        facts.bus = raw.bus;
        facts.address = raw.address;
        facts.port_count = raw.port_count;
        facts.has_bus = raw.has_bus;
        facts.has_address = raw.has_address;
        for (std::size_t i = 0U; i < raw.port_count && i < 8U; ++i)
            facts.ports[i] = raw.ports[i];
        if (facts.has_bus && facts.port_count > 0U) {
            const auto identified = siano_identity(facts);
            if (!identified) return Result<SianoAssembly>::failure(identified.error());
            resolved = identified.value();
        }
    } else {
        const auto located =
            resolve_rio_facts(family->usb(), config.selector, config.selector_text);
        if (!located) {
            std::fprintf(stderr, "assemble: facts %s\n",
                         dtv::userland::error_string(located.error()));
            return Result<SianoAssembly>::failure(located.error());
        }
        facts = located.value();
        const auto identified = siano_identity(facts);
        if (!identified) {
            std::fprintf(stderr, "assemble: identity %s\n",
                         dtv::userland::error_string(identified.error()));
            return Result<SianoAssembly>::failure(identified.error());
        }
        resolved = identified.value();
    }

    SianoAssembly assembly;
    if (resolved) {
        const auto automatic = identity::auto_instance(*resolved);
        if (!automatic) {
            std::fprintf(stderr, "assemble: auto %s\n",
                         dtv::userland::error_string(automatic.error()));
            return Result<SianoAssembly>::failure(automatic.error());
        }
        assembly.lease_identity.assign(automatic.value().bytes.data(),
                                       automatic.value().length);
        if (requested_instance.empty()) {
            assembly.instance = assembly.lease_identity;
        } else {
            const auto canonical = identity::canonicalize_instance(requested_instance);
            if (!canonical) return Result<SianoAssembly>::failure(Error::INVALID_ARGUMENT);
            assembly.instance.assign(canonical.value().bytes.data(),
                                     canonical.value().length);
        }
    } else {
        // --fd without a port path: the explicit instance is the only stable
        // name; it is also the namespace lock identity (non-serial LOCK_SH).
        if (requested_instance.empty())
            return Result<SianoAssembly>::failure(Error::INVALID_ARGUMENT);
        const auto canonical = identity::canonicalize_instance(requested_instance);
        if (!canonical) return Result<SianoAssembly>::failure(Error::INVALID_ARGUMENT);
        assembly.instance.assign(canonical.value().bytes.data(), canonical.value().length);
        assembly.lease_identity = assembly.instance;
    }

    if (!claimed_before_publish) {
        const auto claimed = family->start_device();
        if (!claimed) {
            std::fprintf(stderr, "assemble: claim %s\n",
                         dtv::userland::error_string(claimed.error()));
            return Result<SianoAssembly>::failure(claimed.error());
        }
    }

    SianoFamily* family_ptr = family.get();
    auto assembled = EnclosureContext::assemble(
        std::move(family),
        std::unique_ptr<px4::userland::TunerNonceSource>(
            new (std::nothrow) PlatformTunerNonceSource()));
    if (!assembled) { std::fprintf(stderr, "assemble: context %s\n", dtv::userland::error_string(assembled.error())); return Result<SianoAssembly>::failure(assembled.error()); }
    assembly.family = family_ptr;
    assembly.context = std::move(assembled.value());

    const char* selected_root = runtime_directory;
#if !defined(_WIN32)
    std::array<char, kStoredPathCapacity> automatic_root{};
    if (selected_root == nullptr) {
        const auto selected = select_publish_runtime_root();
        if (!selected) return Result<SianoAssembly>::failure(selected.error());
        automatic_root = selected.value();
        selected_root = automatic_root.data();
    }
#endif

    EnclosurePublication publication{};
    publication.runtime_directory = selected_root;
    publication.instance = assembly.instance;
    publication.lease_identity = assembly.lease_identity;
    publication.access = config.access;
    const auto reserved = assembly.context->reserve_namespace(publication);
    if (!reserved) { std::fprintf(stderr, "assemble: reserve %s\n", dtv::userland::error_string(reserved.error())); return Result<SianoAssembly>::failure(reserved.error()); }

    ControlContext control{};
    const auto built = build_siano_control_context(facts, assembly.instance, epoch,
                                                   product_version,
                                                   assembly.context->tuner_service(), control);
    if (!built) { std::fprintf(stderr, "assemble: build %s\n", dtv::userland::error_string(built.error())); return Result<SianoAssembly>::failure(built.error()); }
    const auto published = assembly.context->publish(control);
    if (!published) { std::fprintf(stderr, "assemble: publish %s\n", dtv::userland::error_string(published.error())); return Result<SianoAssembly>::failure(published.error()); }
    return Result<SianoAssembly>::success(std::move(assembly));
}

}  // namespace siano::runtime
