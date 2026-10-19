/* SPDX-License-Identifier: GPL-2.0-or-later */
// Siano public runtime daemon.  It assembles the canonical P runtime subset
// over the accepted S adapter/Consumer with the same function the proof uses.
#include "siano-enclosure.h"

#include "px4/posix_tuner_nonce.h"
#include "px4d_signals.h"

#include <dtv/userland/identity.h>
#include <dtv/userland/json.h>

extern "C" {
#include "device-selector.h"
#include "siano-device.h"
}

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
        "  --device serial:|usb:  adopted selector; usb:BUS-PORT[.PORT...] is\n"
        "                         required (this enclosure has no usable serial)\n"
        "  --firmware PATH        firmware file passed to the device mode step\n"
        "  --runtime-dir DIR      runtime namespace root\n"
        "  --instance TOKEN       explicit connection instance (default auto)\n"
        "  --group                publish group-accessible endpoints\n"
        "  -h, --help             print this help\n"
        "  -V, --version          print the development version\n"
        "  --models              print static supported model catalog as JSON\n");
}

int write_models()
{
    namespace w = dtv::userland::wire;
    auto copy = [](auto& dest, const char* source) -> bool {
        const std::size_t length = std::strlen(source);
        if (length == 0U || length > dest.bytes.size()) return false;
        dest.length = static_cast<std::uint16_t>(length);
        std::memcpy(dest.bytes.data(), source, length);
        return true;
    };
    // These are the exact three RIO VID:PIDs accepted by is_rio_id().
    // Other named Siano chips in diagnostics are not supported models.
    struct SupportedId { const char* name; std::uint16_t vid, pid; };
    constexpr std::array<SupportedId, 3U> known{{
        {"PX-S1UD", 0x3275U, 0x0080U},
        {"Siano-Rio-0600", 0x187fU, 0x0600U},
        {"Siano-Rio-0302", 0x187fU, 0x0302U},
    }};
    std::array<w::ModelCatalogEntry, known.size()> entries{};
    for (std::size_t i = 0U; i < entries.size(); ++i) {
        auto& entry = entries[i];
        if (!copy(entry.model.id, known[i].name) ||
            !copy(entry.model.name, known[i].name)) return 70;
        entry.usb_ids.count = 1U;
        entry.usb_ids.items[0] = {known[i].vid, known[i].pid, w::Mode::runtime};
        entry.nominal_receiver_count = {true, 1U};
        entry.nominal_usb_function_count = {true, 1U};
        entry.available_in_build = true;
        entry.receivers.present = true;
        entry.receivers.value.count = 1U;
        entry.receivers.value.items[0].id = 0U;
        entry.receivers.value.items[0].systems.mask = 1U;
        entry.max_concurrent_streams = {true, 1U};
    }

    w::ModelsResponse document{};
    if (!copy(document.producer.product, "siano-userland") ||
        !copy(document.producer.version, SIANO_RUNTIME_VERSION)) return 70;
    document.models = {entries.data(), entries.size()};
    const auto size = w::models_json_size(document);
    if (!size) return 70;
    std::vector<std::uint8_t> output(size.value());
    const auto encoded = w::encode_models_json(
        document, {output.data(), output.size()});
    if (!encoded || encoded.value() != output.size() ||
        std::fwrite(output.data(), 1U, output.size(), stdout) != output.size() ||
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
    if (argc == 2 && argv != nullptr && argv[1] != nullptr &&
        std::string_view(argv[1]) == "--models") return write_models();
    std::string device;
    std::string firmware;
    std::string runtime_directory;
    std::string instance;
    bool group = false;
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
        } else {
            std::fprintf(stderr, "argument error: unknown argument\n");
            usage();
            return 2;
        }
    }
    if (device.empty()) {
        std::fprintf(stderr, "argument error: --device is required\n");
        usage();
        return 2;
    }

    siano::runtime::SianoRuntimeConfig config;
    std::string selector_error;
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
    config.firmware = firmware;
    config.own_usb = true;
    config.access = group ? ipc::posix::EndpointAccess::shared_group
                          : ipc::posix::EndpointAccess::private_user;

    ipc::posix::PosixTunerNonceSource epoch_source;
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
    std::fprintf(stderr, "sianod ready: endpoint=%s\n", owned.endpoint_path());

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
    return exit_status(loop_error);
}
