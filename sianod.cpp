/* SPDX-License-Identifier: GPL-2.0-or-later */
// Siano public runtime daemon.  It assembles the canonical P runtime subset
// over the accepted S adapter/Consumer with the same function the proof uses.
#include "siano-enclosure.h"

#ifdef _WIN32
#include "px4/windows_tuner_nonce.h"
#include "px4_windows_args.h"
#else
#include "px4/posix_tuner_nonce.h"
#endif
#include "px4d_signals.h"

#include <dtv/userland/identity.h>
#include <dtv/userland/json.h>
#include <libusb.h>

extern "C" {
#include "device-selector.h"
#include "siano-device.h"
}

#include <array>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifndef SIANO_RUNTIME_VERSION
#define SIANO_RUNTIME_VERSION "0.0.0-dev"
#endif

namespace {

namespace identity = dtv::userland::identity;
using namespace px4::userland;

void usage() noexcept
{
    std::fprintf(stderr,
        "usage: sianod --device usb:BUS-PORT[.PORT...] [--firmware PATH]\n"
        "              [--runtime-dir DIR] [--instance TOKEN] [--group]\n"
        "              [--fd FD] [-p PID ...]\n"
        "  --device serial:|usb:  adopted selector; usb:BUS-PORT[.PORT...] is\n"
        "                         required unless --fd is used\n"
        "  --fd FD                wrap an already-open USB descriptor (Termux/\n"
        "                         Android Host API); a trailing FD is equivalent\n"
        "                         (termux-usb -e). --device is not combined\n"
        "  -p, --pid PID          add a PID filter (repeatable, max 64); 0x2000\n"
        "                         keeps the accepted no-ACK catch-all path\n"
        "  --firmware PATH        firmware file passed to the device mode step\n"
        "  --runtime-dir DIR      runtime namespace root\n"
        "  --instance TOKEN       explicit connection instance (default auto;\n"
        "                         required with --fd when no port path exists)\n"
        "  --group                publish group-accessible endpoints\n"
        "  -h, --help             print this help\n"
        "  -V, --version          print the development version\n"
        "  --models              print static supported models and USB IDs (text)\n"
        "  --list-json           print read-only USB discovery as JSON\n");
}

struct SupportedId { const char* name; std::uint16_t vid, pid; };
constexpr std::array<SupportedId, 3U> kSupportedRioIds{{
    {"PX-S1UD", 0x3275U, 0x0080U},
    {"Siano-Rio-0600", 0x187fU, 0x0600U},
    {"Siano-Rio-0302", 0x187fU, 0x0302U},
}};

int write_models()
{
    namespace w = dtv::userland::wire;
    // Static supported RIO models, not attached USB devices.
    const auto& known = kSupportedRioIds;
    std::array<w::SupportedModel, kSupportedRioIds.size()> entries{};
    for (std::size_t i = 0U; i < entries.size(); ++i) {
        const std::size_t size = std::strlen(known[i].name);
        if (size == 0U || size > entries[i].name.bytes.size()) return 70;
        entries[i].name.length = static_cast<std::uint16_t>(size);
        std::memcpy(entries[i].name.bytes.data(), known[i].name, size);
        entries[i].usb_ids.count = 1U;
        entries[i].usb_ids.items[0] = {known[i].vid, known[i].pid};
    }
    const w::SupportedModels models{{entries.data(), entries.size()}};
    const auto size = w::models_text_size(models);
    if (!size) return 70;
    std::vector<std::uint8_t> output(size.value());
    const auto encoded = w::encode_models_text(models, {output.data(), output.size()});
    if (!encoded || encoded.value() != output.size() ||
        std::fwrite(output.data(), 1U, output.size(), stdout) != output.size() ||
        std::fflush(stdout) != 0) return 70;
    return 0;
}


// Enumerate descriptors only: no interface claim, firmware upload, or tuning.
// Each supported RIO function is a one-function Siano enclosure.
int write_discovery_json()
{
    namespace w = dtv::userland::wire;
    auto copy = [](auto& dest, const char* text) {
        const std::size_t n = std::strlen(text);
        if (n == 0U || n > dest.bytes.size()) return false;
        dest.length = static_cast<std::uint16_t>(n);
        std::memcpy(dest.bytes.data(), text, n);
        return true;
    };
    libusb_context* context = nullptr;
    const int initialized = libusb_init(&context);
    if (initialized != 0) {
        std::fprintf(stderr, "sianod: USB enumeration unavailable (%d)\n", initialized);
        return 7;
    }
    libusb_device** devices = nullptr;
    const ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) {
        std::fprintf(stderr, "sianod: USB device listing failed (%d)\n",
                     static_cast<int>(count));
        libusb_exit(context);
        return 7;
    }
    std::vector<w::DiscoveryFunction> functions;
    std::vector<w::DiscoveryEnclosure> enclosures;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* device = devices[i];
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(device, &descriptor) != 0) continue;
        const SupportedId* profile = nullptr;
        for (const auto& known : kSupportedRioIds) {
            if (known.vid == descriptor.idVendor && known.pid == descriptor.idProduct) {
                profile = &known;
                break;
            }
        }
        if (profile == nullptr) continue;
        if (functions.size() >= std::numeric_limits<std::uint16_t>::max()) {
            libusb_free_device_list(devices, 1);
            libusb_exit(context);
            return 70;
        }
        w::DiscoveryFunction f{};
        auto& observed = f.observation;
        observed.id = static_cast<std::uint16_t>(functions.size());
        observed.vid = descriptor.idVendor;
        observed.pid = descriptor.idProduct;
        observed.mode = w::Mode::runtime;
        observed.present = true;
        observed.serial_state = w::SerialState::not_queried;
        observed.model.present = true;
        if (!copy(observed.model.value.id, profile->name) ||
            !copy(observed.model.value.name, profile->name)) {
            libusb_free_device_list(devices, 1);
            libusb_exit(context);
            return 70;
        }
        observed.location.bus = {true, libusb_get_bus_number(device)};
        observed.location.address = {true, libusb_get_device_address(device)};
        std::uint8_t ports[8]{};
        const int nports = libusb_get_port_numbers(device, ports, sizeof(ports));
        bool has_path = false;
        if (nports > 0 && nports <= static_cast<int>(sizeof(ports))) {
            std::string path = std::to_string(observed.location.bus.value);
            for (int index = 0; index < nports; ++index) {
                path.push_back(index == 0 ? '-' : '.');
                path += std::to_string(ports[index]);
            }
            const std::string selector = "usb:" + path;
            if (identity::parse_device_selector(selector) &&
                path.size() <= observed.location.usb_path.value.bytes.size() &&
                selector.size() <= f.selector_candidates.items[0].value.bytes.size()) {
                auto& usb_path = observed.location.usb_path;
                usb_path.present = true;
                usb_path.value.length = static_cast<std::uint16_t>(path.size());
                std::memcpy(usb_path.value.bytes.data(), path.data(), path.size());
                auto& candidate = f.selector_candidates.items[0];
                candidate.value.length = static_cast<std::uint16_t>(selector.size());
                std::memcpy(candidate.value.bytes.data(), selector.data(), selector.size());
                candidate.state = w::DiscoveryCandidateState::usable;
                f.selector_candidates.count = 1U;
                has_path = true;
            }
        }
        if (has_path) {
            f.grouping = w::DiscoveryGroupingState::bound;
            f.enclosure_id = {true, static_cast<std::uint16_t>(enclosures.size())};
        } else {
            f.grouping = w::DiscoveryGroupingState::unresolved;
            const char* reason = "usb_path_unavailable";
            f.grouping_reason.present = copy(f.grouping_reason.value, reason);
        }
        functions.push_back(std::move(f));
        if (!has_path) continue;
        w::DiscoveryEnclosure enclosure{};
        enclosure.descriptor.id = static_cast<std::uint16_t>(enclosures.size());
        enclosure.descriptor.model.present = true;
        if (!copy(enclosure.descriptor.model.value.id, profile->name) ||
            !copy(enclosure.descriptor.model.value.name, profile->name)) {
            libusb_free_device_list(devices, 1);
            libusb_exit(context);
            return 70;
        }
        enclosure.descriptor.identity.serial_state = w::IdentitySerialState::unknown;
        enclosure.descriptor.identity.uniqueness = w::Uniqueness::unknown;
        enclosure.descriptor.limits.max_concurrent_streams = {true, 1U};
        enclosure.topology.state = w::TopologyState::complete;
        enclosure.topology.expected_function_count = {true, 1U};
        enclosure.usb_function_ids.count = 1U;
        enclosure.usb_function_ids.items[0] = functions.back().observation.id;
        enclosure.selector_candidates.count = 1U;
        enclosure.selector_candidates.items[0] =
            functions.back().selector_candidates.items[0];
        enclosure.receivers.present = true;
        enclosure.receivers.value.count = 1U;
        auto& receiver = enclosure.receivers.value.items[0];
        receiver.id = 0U;
        receiver.systems.mask = 1U;
        receiver.binding.present = true;
        receiver.binding.value = {functions.back().observation.id, 0U};
        enclosure.readers.present = true;
        // Siano has no standard B-CAS reader exposed through its TS runtime.
        enclosures.push_back(std::move(enclosure));
    }
    libusb_free_device_list(devices, 1);
    libusb_exit(context);

    w::DiscoveryResponse document{};
    if (!copy(document.producer.product, "siano-userland") ||
        !copy(document.producer.version, SIANO_RUNTIME_VERSION)) return 70;
    std::random_device entropy;
    for (auto& byte : document.scan_id)
        byte = static_cast<std::uint8_t>(entropy());
    document.usb_functions = {functions.data(), functions.size()};
    document.enclosures = {enclosures.data(), enclosures.size()};
    const auto size = w::discovery_json_size(document);
    if (!size) return 70;
    std::vector<std::uint8_t> bytes(size.value());
    const auto encoded = w::encode_discovery_json(document, {bytes.data(), bytes.size()});
    if (!encoded || encoded.value() != bytes.size() ||
        std::fwrite(bytes.data(), 1U, bytes.size(), stdout) != bytes.size() ||
        std::fflush(stdout) != 0) return 70;
    return 0;
}

constexpr std::chrono::milliseconds kWholeContextCloseBudget{15000};
constexpr int kCloseDeadlineExitStatus = 70;

int exit_status(Error error) noexcept
{
    switch (error) {
    case Error::OK: return 0;
    case Error::INVALID_ARGUMENT: return 2;
    case Error::NOT_FOUND:
    case Error::NOT_READY:
    case Error::UNSUPPORTED: return 3;
    case Error::BUSY: return 4;
    case Error::TIMEOUT: return 5;
    case Error::PROTOCOL_ERROR: return 6;
    case Error::USB_IO:
    case Error::DISCONNECTED: return 7;
    case Error::SLOW_CONSUMER: return 8;
    case Error::CARD_REMOVED:
    case Error::BUFFER_TOO_SMALL: return 9;
    case Error::FIRMWARE_REJECTED: return 10;
    case Error::RESOURCE_LIMIT: return 4;
    case Error::INTERNAL: return 70;
    }
    return 70;
}

bool parse_number(std::string_view text, std::uint32_t& out) noexcept
{
    if (text.empty()) return false;
    std::uint32_t value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
    out = value;
    return true;
}

// The accepted standalone parser used strtoul(..., 0): decimal or 0x hex and a
// maximum of 0x2000 (the firmware's no-ACK catch-all).
bool parse_pid(std::string_view text, std::uint16_t& out) noexcept
{
    int base = 10;
    if (text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2U);
        base = 16;
    } else if (text.size() > 1U && text[0] == '0') {
        base = 8;
    }
    if (text.empty()) return false;
    std::uint32_t value = 0U;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value > 0x2000U)
        return false;
    out = static_cast<std::uint16_t>(value);
    return true;
}

// Maps the adopted public selector onto the internal S selector.  A serial
// selection is reported NOT_FOUND because the RIO exposes no usable serial;
// no placeholder serial is invented.
enum class SelectorStatus { ok, not_found, invalid };

SelectorStatus map_public_selector(const std::string& text,
                                   siano_device_selector& out,
                                   std::string& error) noexcept
{
    const auto parsed = identity::parse_device_selector(text);
    if (!parsed) { error = "invalid device selector"; return SelectorStatus::invalid; }
    if (parsed.value().kind == identity::SelectorKind::serial) {
        error = "no usable serial on this enclosure";
        return SelectorStatus::not_found;
    }
    if (parsed.value().topology.count != 1U) {
        error = "exactly one usb topology path is required";
        return SelectorStatus::invalid;
    }
    const auto& path = parsed.value().topology.paths[0];
    const std::string_view value(path.bytes.data(), path.length);
    const std::size_t dash = value.find('-');
    if (dash == std::string_view::npos) { error = "usb path is malformed"; return SelectorStatus::invalid; }
    std::uint32_t bus = 0U;
    if (!parse_number(value.substr(0U, dash), bus) || bus > 0xffU) {
        error = "usb bus is invalid"; return SelectorStatus::invalid;
    }
    out = siano_device_selector{};
    out.kind = SIANO_DEVICE_SELECTOR_PORT;
    out.bus = static_cast<std::uint8_t>(bus);
    std::string_view rest = value.substr(dash + 1U);
    while (!rest.empty()) {
        const std::size_t dot = rest.find('.');
        const std::string_view part = rest.substr(0U, dot);
        std::uint32_t port = 0U;
        if (!parse_number(part, port) || port > 0xffU || out.port_count >= 8U) {
            error = "usb port path is invalid"; return SelectorStatus::invalid;
        }
        out.ports[out.port_count++] = static_cast<std::uint8_t>(port);
        if (dot == std::string_view::npos) break;
        rest = rest.substr(dot + 1U);
    }
    if (out.port_count == 0U) { error = "usb path has no ports"; return SelectorStatus::invalid; }
    return SelectorStatus::ok;
}

}  // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    auto owned_argv = cli::windows_argv_utf8(argc, argv);
    std::vector<char*> argv_views;
    for (auto& argument : owned_argv) argv_views.push_back(argument.data());
    if (argv_views.empty()) return 2;
    argc = static_cast<int>(argv_views.size());
    argv = argv_views.data();
#endif
    if (argc == 2 && argv != nullptr && argv[1] != nullptr) {
        const std::string_view option(argv[1]);
        if (option == "--models") return write_models();
        if (option == "--list-json") return write_discovery_json();
    }
    std::string device;
    std::string firmware;
    std::string runtime_directory;
    std::string instance;
    bool group = false;
    int device_fd = -1;
    siano::runtime::SianoRuntimeConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i] != nullptr ? argv[i] : "");
        auto need_value = [&](std::string& target) noexcept {
            if (i + 1 >= argc || argv[i + 1] == nullptr) return false;
            target.assign(argv[++i]);
            return true;
        };
        if (option == "-h" || option == "--help") { usage(); return 0; }
        if (option == "-V" || option == "--version") {
            std::fprintf(stdout, "sianod %s (development; not a v1 release)\n",
                         SIANO_RUNTIME_VERSION);
            return 0;
        }
        if (option == "--device") {
            if (!device.empty() || !need_value(device)) {
                std::fprintf(stderr, "argument error: --device\n"); return 2;
            }
        } else if (option == "--fd") {
            std::string text;
            std::uint32_t value = 0U;
            if (device_fd >= 0 || !need_value(text) || !parse_number(text, value) ||
                value > static_cast<std::uint32_t>(INT_MAX)) {
                std::fprintf(stderr, "argument error: --fd\n"); return 2;
            }
            device_fd = static_cast<int>(value);
        } else if (option == "-p" || option == "--pid") {
            std::string text;
            std::uint16_t pid = 0U;
            if (!need_value(text) || !parse_pid(text, pid) ||
                config.pid_count >= config.pids.size()) {
                std::fprintf(stderr, "argument error: --pid\n"); return 2;
            }
            config.pids[config.pid_count++] = pid;
        } else if (option == "--firmware") {
            if (!firmware.empty() || !need_value(firmware)) {
                std::fprintf(stderr, "argument error: --firmware\n"); return 2;
            }
        } else if (option == "--runtime-dir") {
            if (!runtime_directory.empty() || !need_value(runtime_directory)) {
                std::fprintf(stderr, "argument error: --runtime-dir\n"); return 2;
            }
        } else if (option == "--instance") {
            if (!instance.empty() || !need_value(instance)) {
                std::fprintf(stderr, "argument error: --instance\n"); return 2;
            }
        } else if (option == "--group") {
            group = true;
        } else if (!option.empty() && option.front() != '-' && i == argc - 1) {
            std::uint32_t value = 0U;
            if (device_fd >= 0 || !parse_number(option, value) ||
                value > static_cast<std::uint32_t>(INT_MAX)) {
                std::fprintf(stderr, "argument error: USB fd\n"); return 2;
            }
            device_fd = static_cast<int>(value);
        } else {
            std::fprintf(stderr, "argument error: unknown argument\n");
            usage();
            return 2;
        }
    }
    if (device_fd < 0 && device.empty()) {
        std::fprintf(stderr, "argument error: --device or --fd is required\n");
        usage();
        return 2;
    }
    if (device_fd >= 0 && !device.empty()) {
        std::fprintf(stderr, "argument error: --device cannot be combined with --fd\n");
        return 2;
    }

    std::string selector_error;
    if (device_fd < 0) {
        const auto status = map_public_selector(device, config.selector, selector_error);
        if (status == SelectorStatus::invalid) {
            std::fprintf(stderr, "device: %s\n", selector_error.c_str());
            return 2;
        }
        if (status == SelectorStatus::not_found) {
            std::fprintf(stderr, "device: %s\n", selector_error.c_str());
            return 3;
        }
        config.selector_text = device;
    } else {
        config.device_fd = device_fd;
    }
    config.firmware = firmware;
    config.own_usb = true;
    config.access = group ? ipc::posix::EndpointAccess::shared_group
                          : ipc::posix::EndpointAccess::private_user;

#ifdef _WIN32
    ipc::windows::WindowsTunerNonceSource epoch_source;
#else
    ipc::posix::PosixTunerNonceSource epoch_source;
#endif
    const auto epoch = epoch_source.generate();
    if (!epoch) {
        std::fprintf(stderr, "context epoch: %s\n", error_string(epoch.error()));
        return exit_status(epoch.error());
    }

    auto assembly = siano::runtime::assemble_siano_context(
        config, instance, runtime_directory.empty() ? nullptr : runtime_directory.c_str(),
        epoch.value(), SIANO_RUNTIME_VERSION);
    if (!assembly) {
        std::fprintf(stderr, "sianod: %s\n", error_string(assembly.error()));
        return exit_status(assembly.error());
    }
    ipc::posix::EnclosureContext& owned = *assembly.value().context;

    if (!px4::userland::px4d::install_signal_handlers()) {
        std::fprintf(stderr, "signal setup failed\n");
        return 70;
    }
#if defined(_WIN32)
    std::fprintf(stderr, "sianod ready: endpoint=%ls\n", owned.endpoint_path());
#else
    std::fprintf(stderr, "sianod ready: endpoint=%s\n", owned.endpoint_path());
#endif

    Error loop_error = Error::OK;
    while (!px4::userland::px4d::stop_requested()) {
        const auto polled = owned.poll_once(Timeout{100U});
        if (!polled) { loop_error = polled.error(); break; }
    }
    const ipc::posix::ContextCloseResult closed = owned.close(kWholeContextCloseBudget);
    if (!closed.quiesced) {
        px4::userland::px4d::exit_after_close_deadline(kCloseDeadlineExitStatus);
    }
    if (closed.error != Error::OK) {
        std::fprintf(stderr, "shutdown cleanup: %s\n", error_string(closed.error));
        if (loop_error == Error::OK) loop_error = closed.error;
    }
    if (loop_error != Error::OK) {
        std::fprintf(stderr, "sianod stopped: %s\n", error_string(loop_error));
    }
    px4::userland::px4d::notify_cleanup_complete();
    return exit_status(loop_error);
}
