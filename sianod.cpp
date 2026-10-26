/* SPDX-License-Identifier: GPL-2.0-or-later */
// Siano public runtime daemon.  It assembles the canonical P runtime subset
// over the accepted S adapter/Consumer with the same function the proof uses.
#include "siano-enclosure.h"
#include "siano-discovery.h"

#ifdef _WIN32
#include "px4/windows_tuner_nonce.h"
#include "px4_windows_args.h"
#include <dtv/userland/host_facade/windows_stdin.h>
#else
#include "px4/posix_tuner_nonce.h"
#endif
#include "px4d_signals.h"
#include "sianod-daemon-lifecycle.h"

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
namespace host_runtime = dtv::userland::host_runtime;
namespace host_context = dtv::userland::host_listener_context;
using namespace px4::userland;

void usage() noexcept
{
    std::fprintf(stderr,
        "usage: sianod --device usb:BUS-PORT[.PORT...] [--firmware PATH]\n"
        "              [--runtime-dir DIR] [--instance TOKEN] [--group]\n"
        "              [--fd FD] [-p PID ...]\n"
#ifdef _WIN32
        "              [--exit-on-stdin-eof]\n"
#endif
        "  --device usb:BUS-PORT[.PORT...] select by USB topology; required\n"
        "                         unless --fd is used (example: usb:1-2.3)\n"
        "                         serial: is accepted only when a usable serial exists\n"
        "  --fd FD                wrap an already-open USB descriptor (Termux/\n"
        "                         Android Host API); a trailing FD is equivalent\n"
        "                         (termux-usb -e). --device is not combined\n"
        "  -p, --pid PID          add a PID filter (repeatable, max 64); 0x2000\n"
        "                         keeps the accepted no-ACK catch-all path\n"
        "  --firmware PATH        firmware file passed to the device mode step\n"
        "  --runtime-dir DIR      runtime namespace root; the completed socket path\n"
        "                         must fit this OS build's native socket field\n"
        "  --instance TOKEN       explicit connection instance (default auto;\n"
        "                         required with --fd when no port path exists)\n"
        "                         1..64 ASCII bytes: start alphanumeric, then\n"
        "                         alphanumeric, '_', '.', or '-'; no trailing dot\n"
        "                         or reserved Windows device name\n"
        "  --group                publish group-accessible endpoints\n"
#ifndef _WIN32
        "                         (requires --runtime-dir on POSIX)\n"
#endif
#ifdef _WIN32
        "  --exit-on-stdin-eof    stop when the parent closes stdin\n"
#endif
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
    for (const auto& model : kSupportedRioIds) {
        if (std::fprintf(stdout, "%s %04x:%04x\n", model.name,
                         model.vid, model.pid) < 0) return 70;
    }
    if (std::fflush(stdout) != 0) return 70;
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
    std::vector<w::DiscoveryDevice> discovered_devices;
    std::vector<w::DiscoveryReceiver> receivers;
    std::vector<w::DiscoveryEnclosure> enclosures;
    std::vector<w::DiscoveryUsbObservation> ungrouped;
    std::vector<w::DiscoveryScanIssue> issues;
    bool descriptor_unavailable = false;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* device = devices[i];
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(device, &descriptor) != 0) {
            descriptor_unavailable = true;
            continue;
        }
        const SupportedId* profile = nullptr;
        for (const auto& known : kSupportedRioIds) {
            if (known.vid == descriptor.idVendor && known.pid == descriptor.idProduct) {
                profile = &known;
                break;
            }
        }
        if (profile == nullptr) continue;
        if (discovered_devices.size() >= std::numeric_limits<std::uint16_t>::max()) {
            libusb_free_device_list(devices, 1);
            libusb_exit(context);
            return 70;
        }
        w::DiscoveryDevice observed{};
        observed.device = static_cast<std::uint16_t>(discovered_devices.size());
        observed.vid = descriptor.idVendor;
        observed.pid = descriptor.idProduct;
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
                path.size() <= observed.location.port.value.bytes.size()) {
                observed.location.port.present = true;
                if (!copy(observed.location.port.value, path.c_str())) {
                    libusb_free_device_list(devices, 1);
                    libusb_exit(context);
                    return 70;
                }
                has_path = true;
            }
        }
        if (!has_path) {
            w::DiscoveryUsbObservation observation{};
            observation.model.present = copy(observation.model.value, profile->name);
            observation.vid = descriptor.idVendor;
            observation.pid = descriptor.idProduct;
            observation.status = w::DiscoveryObservationStatus::unknown;
            observation.location = observed.location;
            ungrouped.push_back(observation);
            continue;
        }
        const auto device_id = observed.device;
        discovered_devices.push_back(observed);
        w::DiscoveryReceiver receiver{};
        receiver.receiver = static_cast<std::uint16_t>(receivers.size());
        receiver.device = device_id;
        receiver.local = 0U;
        receiver.systems.mask = 1U;
        receivers.push_back(receiver);
        w::DiscoveryEnclosure enclosure{};
        if (!make_siano_discovery_enclosure(enclosure,
                static_cast<std::uint16_t>(enclosures.size()), profile->name)) {
            libusb_free_device_list(devices, 1);
            libusb_exit(context);
            return 70;
        }
        enclosures.push_back(std::move(enclosure));
    }
    libusb_free_device_list(devices, 1);
    libusb_exit(context);

    if (descriptor_unavailable) {
        w::DiscoveryScanIssue issue{};
        if (!copy(issue.code, "usb_descriptor_unavailable") ||
            !copy(issue.detail, "Some USB device descriptors could not be read"))
            return 70;
        issues.push_back(issue);
    }

    for (std::size_t i = 0U; i < enclosures.size(); ++i) {
        enclosures[i].devices = &discovered_devices[i];
        discovered_devices[i].receivers_known = true;
        discovered_devices[i].receiver_count = 1U;
        discovered_devices[i].receivers = &receivers[i];
    }
    w::DiscoveryScan document{};
    document.complete = !descriptor_unavailable;
    if (!copy(document.product, "siano-userland") ||
        !copy(document.version, SIANO_RUNTIME_VERSION)) return 70;
    document.issue_count = issues.size();
    document.issues = issues.data();
    document.enclosure_count = enclosures.size();
    document.enclosures = enclosures.data();
    document.ungrouped_usb_device_count = ungrouped.size();
    document.ungrouped_usb_devices = ungrouped.data();
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
    if (!parsed) {
        error = "--device expects usb:BUS-PORT[.PORT...] (for example usb:1-2.3)";
        return SelectorStatus::invalid;
    }
    if (parsed.value().kind == identity::SelectorKind::serial) {
        error = "--device serial: cannot be resolved because this enclosure has no usable serial; use usb:BUS-PORT[.PORT...]";
        return SelectorStatus::not_found;
    }
    if (parsed.value().topology.count != 1U) {
        error = "--device requires exactly one USB topology path: usb:BUS-PORT[.PORT...]";
        return SelectorStatus::invalid;
    }
    const auto& path = parsed.value().topology.paths[0];
    const std::string_view value(path.bytes.data(), path.length);
    const std::size_t dash = value.find('-');
    if (dash == std::string_view::npos) {
        error = "--device USB path must be usb:BUS-PORT[.PORT...]";
        return SelectorStatus::invalid;
    }
    std::uint32_t bus = 0U;
    if (!parse_number(value.substr(0U, dash), bus) || bus > 0xffU) {
        error = "--device USB bus must be a decimal value in 0..255";
        return SelectorStatus::invalid;
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
            error = "--device USB ports must be decimal values in 0..255 with at most 8 levels";
            return SelectorStatus::invalid;
        }
        out.ports[out.port_count++] = static_cast<std::uint8_t>(port);
        if (dot == std::string_view::npos) break;
        rest = rest.substr(dot + 1U);
    }
    if (out.port_count == 0U) {
        error = "--device requires at least one USB port after the bus";
        return SelectorStatus::invalid;
    }
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
#ifdef _WIN32
    bool exit_on_stdin_eof = false;
#endif
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
#ifdef _WIN32
        } else if (option == "--exit-on-stdin-eof") {
            if (exit_on_stdin_eof) {
                std::fprintf(stderr, "argument error: --exit-on-stdin-eof\n");
                return 2;
            }
            exit_on_stdin_eof = true;
#endif
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
    config.access = group ? host_runtime::EndpointAccess::shared_group
                          : host_runtime::EndpointAccess::private_user;

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

    siano::runtime::AssemblyDiagnostic assembly_diagnostic{};
    auto assembly = siano::runtime::assemble_siano_context(
        config, instance, runtime_directory.empty() ? nullptr : runtime_directory.c_str(),
        epoch.value(), SIANO_RUNTIME_VERSION, &assembly_diagnostic);
    if (!assembly) {
        const std::string detail = siano::runtime::describe_assembly_diagnostic(
            assembly_diagnostic);
        if (!detail.empty())
            std::fprintf(stderr, "sianod: %s: %s\n", error_string(assembly.error()),
                         detail.c_str());
        else
            std::fprintf(stderr, "sianod: %s\n", error_string(assembly.error()));
        return exit_status(assembly.error());
    }
    host_context::EnclosureContext& owned = *assembly.value().context;

    if (!px4::userland::px4d::install_signal_handlers()) {
        std::fprintf(stderr, "signal setup failed\n");
        return 70;
    }
    std::fprintf(stderr, "sianod ready: endpoint=%s\n",
                 assembly.value().endpoint_path.c_str());

    Error loop_error = Error::OK;
    bool close_pending = false;
    const int status = px4::userland::run_sianod_daemon_lifecycle(
#ifdef _WIN32
        exit_on_stdin_eof,
#else
        false,
#endif
        [&]() noexcept {
            if (px4::userland::px4d::stop_requested()) return;
            const auto polled = owned.poll_once(Timeout{100U});
            if (!polled) {
                loop_error = polled.error();
                px4::userland::px4d::request_stop();
            }
        },
        [&]() noexcept {
            const auto closed = owned.close(Timeout{
                static_cast<std::uint32_t>(kWholeContextCloseBudget.count())});
            if (closed && closed.value() == host_context::CloseState::pending) {
                close_pending = true;
            }
            if (!closed) {
                std::fprintf(stderr, "shutdown cleanup: %s\n", error_string(closed.error()));
                if (loop_error == Error::OK) loop_error = closed.error();
            }
            if (loop_error != Error::OK) {
                std::fprintf(stderr, "sianod stopped: %s\n", error_string(loop_error));
            }
            return exit_status(loop_error);
        },
        [&]() noexcept {
            if (close_pending) {
                px4::userland::px4d::exit_after_close_deadline(kCloseDeadlineExitStatus);
            }
        });
    return status;
}
