/* SPDX-License-Identifier: GPL-2.0-or-later */
// Siano public runtime TS client entry.  The adopted shared CLI resolves the
// channel/Hz in the common tuning authority; the exact Hz is passed to the
// shared wire runner unchanged.
#include "px4_ts_core.h"
#include "px4_ts_posix.h"

#include <dtv/userland/records.h>
#include <dtv/userland/tuning.h>

#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <signal.h>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

#ifndef SIANO_RUNTIME_VERSION
#define SIANO_RUNTIME_VERSION "0.0.0-dev"
#endif

namespace {

using namespace px4::userland;
using namespace px4::userland::cli;

volatile std::sig_atomic_t stop_flag = 0;
void stop_handler(int) noexcept { stop_flag = 1; }

class PosixClock final : public Px4TsClock {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

class PosixSignal final : public Px4TsSignal {
public:
    bool stop_requested() const noexcept override { return stop_flag != 0; }
};

bool install_signals() noexcept
{
    struct sigaction ignore_pipe {};
    ignore_pipe.sa_handler = SIG_IGN;
    if (sigemptyset(&ignore_pipe.sa_mask) != 0 ||
        ::sigaction(SIGPIPE, &ignore_pipe, nullptr) != 0) return false;
    struct sigaction action {};
    action.sa_handler = stop_handler;
    if (sigemptyset(&action.sa_mask) != 0) return false;
    return ::sigaction(SIGINT, &action, nullptr) == 0 &&
           ::sigaction(SIGTERM, &action, nullptr) == 0;
}

void usage() noexcept
{
    std::fprintf(stderr,
        "usage: siano-ts (-c CHANNEL | -f HZ [--system isdb-t|isdb-s]) --instance TOKEN\n"
        "                [-t SECONDS|-] [-o FILE|-] [--runtime-dir DIR]\n"
        "                [--receiver 0] [--tune-timeout-ms MS] [--packet-count N]\n"
        "  -c, --channel TEXT     adopted channel (13..62/T13..T62, BS01_0, CS2)\n"
        "  -f, --freq HZ          exact requested Hz (1..2^53-1)\n"
        "      --system SYS       isdb-t or isdb-s (required with --freq)\n"
        "  -t, --time SECONDS     capture duration; '-' is unlimited\n"
        "  -o, --output FILE      output file; '-' is stdout\n"
        "      --runtime-dir DIR  runtime namespace root\n"
        "      --instance TOKEN   connection instance token (required)\n"
        "      --receiver N       receiver index (only 0 on this enclosure)\n"
        "      --tune-timeout-ms  tune end-to-end timeout in ms\n"
        "      --packet-count N   stop after N written 188-byte packets\n"
        "  -h, --help             print this help\n"
        "  -V, --version          print the development version\n");
}

bool parse_unsigned(std::string_view text, std::uint64_t& out) noexcept
{
    if (text.empty()) return false;
    std::uint64_t value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
    out = value;
    return true;
}

struct SianoArgs final {
    bool valid = false;
    bool help = false;
    bool version = false;
    std::string error;
    std::string channel;
    std::string output = "-";
    std::string runtime_directory;
    std::string instance;
    std::string system;
    bool have_channel = false;
    bool have_frequency = false;
    bool have_system = false;
    std::uint64_t frequency_hz = 0U;
    std::uint64_t duration_seconds = 0U;
    bool duration_set = false;
    std::uint64_t packet_count = 0U;
    bool packet_count_set = false;
    std::uint64_t receiver = 0U;
    std::uint64_t tune_timeout_ms = 10000U;
};

SianoArgs parse_arguments(int argc, const char* const* argv) noexcept
{
    SianoArgs args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i] != nullptr ? argv[i] : "");
        auto need_value = [&](std::string_view& value) noexcept {
            if (i + 1 >= argc || argv[i + 1] == nullptr) return false;
            value = std::string_view(argv[++i]);
            return true;
        };
        std::string_view value;
        if (option == "-h" || option == "--help") { args.help = true; continue; }
        if (option == "-V" || option == "--version") { args.version = true; continue; }
        if (option == "-c" || option == "--channel") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (args.have_channel) { args.error = "duplicate --channel"; return args; }
            args.channel.assign(value.data(), value.size());
            args.have_channel = true;
        } else if (option == "-f" || option == "--freq") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (args.have_frequency) { args.error = "duplicate --freq"; return args; }
            if (!parse_unsigned(value, args.frequency_hz)) {
                args.error = "frequency is invalid"; return args;
            }
            args.have_frequency = true;
        } else if (option == "--system") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (args.have_system) { args.error = "duplicate --system"; return args; }
            if (value != "isdb-t" && value != "isdb-s") {
                args.error = "system must be isdb-t or isdb-s"; return args;
            }
            args.system.assign(value.data(), value.size());
            args.have_system = true;
        } else if (option == "-t" || option == "--time") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (args.duration_set) { args.error = "duplicate --time"; return args; }
            args.duration_set = true;
            if (value != "-") {
                if (!parse_unsigned(value, args.duration_seconds) ||
                    args.duration_seconds == 0U) {
                    args.error = "time is invalid"; return args;
                }
            }
        } else if (option == "-o" || option == "--output") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (args.output != "-") { args.error = "duplicate --output"; return args; }
            args.output.assign(value.data(), value.size());
        } else if (option == "--runtime-dir") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (!args.runtime_directory.empty()) {
                args.error = "duplicate --runtime-dir"; return args;
            }
            args.runtime_directory.assign(value.data(), value.size());
        } else if (option == "--instance") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (!args.instance.empty()) { args.error = "duplicate --instance"; return args; }
            args.instance.assign(value.data(), value.size());
        } else if (option == "--receiver") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (!parse_unsigned(value, args.receiver) || args.receiver >= 8U) {
                args.error = "receiver must be 0..7"; return args;
            }
        } else if (option == "--tune-timeout-ms") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (!parse_unsigned(value, args.tune_timeout_ms)) {
                args.error = "tune-timeout-ms is invalid"; return args;
            }
        } else if (option == "--packet-count") {
            if (!need_value(value)) { args.error = "option requires a value"; return args; }
            if (args.packet_count_set || !parse_unsigned(value, args.packet_count) ||
                args.packet_count == 0U) {
                args.error = "packet-count is invalid"; return args;
            }
            args.packet_count_set = true;
        } else {
            args.error = "unknown argument";
            return args;
        }
    }
    if (args.help || args.version) { args.valid = true; return args; }
    if (args.have_channel == args.have_frequency) {
        args.error = "exactly one of --channel or --freq is required";
        return args;
    }
    if (args.instance.empty()) { args.error = "--instance is required"; return args; }
    if (args.have_frequency && !args.have_system) {
        args.error = "--system is required with --freq"; return args;
    }
    if (args.duration_set && args.packet_count_set) {
        args.error = "--time and --packet-count cannot be combined"; return args;
    }
    if (args.tune_timeout_ms < 100U || args.tune_timeout_ms > 30000U) {
        args.error = "tune-timeout-ms must be 100..30000"; return args;
    }
    args.valid = true;
    return args;
}

ipc::System to_ipc(dtv::userland::wire::System system) noexcept
{
    return system == dtv::userland::wire::System::isdb_s ? ipc::System::ISDB_S
                                                         : ipc::System::ISDB_T;
}

}  // namespace

int main(int argc, char** argv)
{
    const SianoArgs args = parse_arguments(argc, const_cast<const char* const*>(argv));
    if (!args.valid) {
        std::fprintf(stderr, "argument error: %s\n", args.error.c_str());
        usage();
        return 2;
    }
    if (args.help) { usage(); return 0; }
    if (args.version) {
        std::fprintf(stdout, "siano-ts %s (development; not a v1 release)\n",
                     SIANO_RUNTIME_VERSION);
        return 0;
    }

    dtv::tuning::Request request{};
    if (args.have_channel) request.channel = std::string_view(args.channel);
    if (args.have_frequency) request.frequency_hz = args.frequency_hz;
    if (args.have_system) {
        request.system = args.system == "isdb-s" ? dtv::userland::wire::System::isdb_s
                                                 : dtv::userland::wire::System::isdb_t;
    }
    const auto resolved = dtv::tuning::resolve(request);
    if (!resolved) {
        std::fprintf(stderr, "tuning: %s\n", error_string(resolved.error()));
        return 2;
    }

    Px4TsArguments arguments{};
    arguments.valid = true;
    arguments.instance = args.instance;
    arguments.runtime_directory = args.runtime_directory;
    arguments.output = args.output;
    arguments.receiver = static_cast<std::uint8_t>(args.receiver);
    arguments.system = to_ipc(resolved.value().system);
    arguments.frequency_hz = resolved.value().frequency_hz;
    if (resolved.value().selection == dtv::userland::wire::Selection::tsid) {
        arguments.stream_id = resolved.value().selection_value;
    } else if (resolved.value().selection == dtv::userland::wire::Selection::slot) {
        arguments.slot = resolved.value().selection_value;
    }
    arguments.tune_timeout_ms = static_cast<std::uint32_t>(args.tune_timeout_ms);
    if (args.duration_set && args.duration_seconds != 0U) {
        arguments.duration_seconds = args.duration_seconds;
        arguments.duration_set = true;
    }
    if (args.packet_count_set) {
        arguments.packet_count = args.packet_count;
        arguments.packet_count_set = true;
    }

    if (!install_signals()) {
        std::fprintf(stderr, "signal setup failed\n");
        return 70;
    }
    const bool stdout_output = arguments.output == "-";
    int fd = STDOUT_FILENO;
    if (!stdout_output) {
        fd = ::open(arguments.output.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (fd < 0) {
            std::fprintf(stderr, "output: %s\n", std::strerror(errno));
            return 70;
        }
    }
    Px4TsFdOutput output(fd, !stdout_output);
    if (!output.ready()) {
        std::fprintf(stderr, "output setup failed\n");
        return 70;
    }
    PosixClock clock;
    PosixSignal signal;
    Px4TsFailureKind failure_kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics;
    const auto result = Px4TsRunner::run_wire(arguments, output, clock, signal,
                                              &failure_kind, &diagnostics);
    if (!result) {
        std::fprintf(stderr, "siano-ts: %s\n", error_string(result.error()));
        std::fprintf(stderr, "written packets=%llu bytes=%llu\n",
                     static_cast<unsigned long long>(diagnostics.packets_written),
                     static_cast<unsigned long long>(diagnostics.packets_written * 188U));
        return px4_ts_exit_status(result.error(), failure_kind);
    }
    return 0;
}
