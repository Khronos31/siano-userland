/* SPDX-License-Identifier: GPL-2.0-or-later */
// Siano public runtime TS client entry.  The adopted shared CLI resolves the
// channel/Hz in the common tuning authority; the exact Hz is passed to the
// shared wire runner unchanged.
#include <dtv/host_ts/px4_ts_core.h>
#ifdef _WIN32
#include <dtv/host_ts/px4_ts_windows_output.h>
#include "px4_windows_args.h"
#include "px4d_signals.h"
#else
#include <dtv/host_ts/px4_ts_posix.h>
#include <unistd.h>
#endif

extern "C" {
#include "control-input.h"
#include "control-parse.h"
}

#include <dtv/userland/records.h>
#include <dtv/userland/tuning.h>

#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <memory>
#include <new>
#include <signal.h>
#include <string>
#include <string_view>
#include <thread>

#ifndef SIANO_RUNTIME_VERSION
#define SIANO_RUNTIME_VERSION "0.0.0-dev"
#endif

namespace {

using namespace px4::userland;
using namespace px4::userland::cli;

std::string default_runtime_directory() noexcept
{
#ifdef _WIN32
    const char* base = std::getenv("LOCALAPPDATA");
    if (base == nullptr || base[0] == '\0') base = std::getenv("TEMP");
    return base != nullptr ? std::string(base) : std::string();
#else
    const char* base = std::getenv("XDG_RUNTIME_DIR");
    if (base == nullptr || base[0] == '\0') base = std::getenv("TMPDIR");
    if (base == nullptr || base[0] == '\0') base = "/tmp";
    if (base[0] != '/') return {};
    std::string path(base);
    while (path.size() > 1U && path.back() == '/') path.pop_back();
    path += "/dtv-userland-";
    path += std::to_string(static_cast<unsigned long long>(::geteuid()));
    return path;
#endif
}

volatile std::sig_atomic_t stop_flag = 0;
void stop_handler(int) noexcept { stop_flag = 1; }
bool platform_stop_requested() noexcept
{
#ifdef _WIN32
    return px4::userland::px4d::stop_requested();
#else
    return stop_flag != 0;
#endif
}

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
    bool stop_requested() const noexcept override { return platform_stop_requested(); }
};

bool install_signals() noexcept
{
#ifdef _WIN32
    return px4::userland::px4d::install_signal_handlers();
#else
    struct sigaction ignore_pipe {};
    ignore_pipe.sa_handler = SIG_IGN;
    if (sigemptyset(&ignore_pipe.sa_mask) != 0 ||
        ::sigaction(SIGPIPE, &ignore_pipe, nullptr) != 0) return false;
    struct sigaction action {};
    action.sa_handler = stop_handler;
    if (sigemptyset(&action.sa_mask) != 0) return false;
    return ::sigaction(SIGINT, &action, nullptr) == 0 &&
           ::sigaction(SIGTERM, &action, nullptr) == 0;
#endif
}

std::unique_ptr<Px4TsOutput> open_output(const std::string& path) noexcept
{
    const bool stdout_output = path == "-";
#ifdef _WIN32
    HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (!stdout_output) {
        const auto wide = utf8_to_wide_path(path);
        if (wide.empty()) return {};
        handle = ::CreateFileW(wide.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return {};
    auto created = Px4TsWindowsOutput::create(handle, !stdout_output);
    if (!created) return {};
    return std::move(created.value());
#else
    const int fd = stdout_output ? STDOUT_FILENO :
        ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) return {};
    std::unique_ptr<Px4TsFdOutput> output(new (std::nothrow) Px4TsFdOutput(fd, !stdout_output));
    if (!output) {
        if (!stdout_output) ::close(fd);
        return {};
    }
    if (!output->ready()) return {};
    return output;
#endif
}

Result<void> finish_output(Px4TsOutput& output) noexcept
{
#ifdef _WIN32
    auto& windows = static_cast<Px4TsWindowsOutput&>(output);
    const auto finished = windows.finish();
    if (windows.worker_stopped()) px4::userland::px4d::notify_cleanup_complete();
    return finished;
#else
    (void)output;
    return Result<void>::success();
#endif
}

void usage() noexcept
{
    std::fprintf(stderr,
        "usage: siano-ts (-c CHANNEL | -f HZ) [--system ISDB-T] --instance TOKEN\n"
        "                [-t SECONDS|-] [-o FILE|-] [--runtime-dir DIR]\n"
        "                [--receiver 0] [--tune-timeout-ms MS] [--packet-count N]\n"
        "                [--control]\n"
        "  -c, --channel TEXT     ISDB-T channel (13..62/T13..T62)\n"
        "  -f, --freq HZ          exact requested Hz (1..2^53-1)\n"
        "      --system SYS       ISDB-T (optional, case-insensitive)\n"
        "  -t, --time SECONDS     capture duration; '-' is unlimited\n"
        "  -o, --output FILE      output file; '-' is stdout\n"
        "      --control          accept channel N/tune HZ/quit on stdin and\n"
        "                         re-select through the shared UDTV API\n"
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
    bool control = false;
    std::uint64_t frequency_hz = 0U;
    std::uint64_t duration_seconds = 0U;
    bool duration_set = false;
    std::uint64_t packet_count = 0U;
    bool packet_count_set = false;
    std::uint64_t receiver = 0U;
    std::uint64_t tune_timeout_ms = 10000U;
};

bool ascii_equals(std::string_view input, std::string_view canonical) noexcept
{
    if (input.size() != canonical.size()) return false;
    for (std::size_t i = 0U; i < input.size(); ++i) {
        char ch = input[i];
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        if (ch != canonical[i]) return false;
    }
    return true;
}

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
            if (!ascii_equals(value, "isdb-t") && !ascii_equals(value, "isdb-s")) {
                args.error = "system must be ISDB-T or ISDB-S"; return args;
            }
            args.system = ascii_equals(value, "isdb-s") ? "isdb-s" : "isdb-t";
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
        } else if (option == "--control") {
            if (args.control) { args.error = "duplicate --control"; return args; }
            args.control = true;
        } else if (option == "-p" || option == "--pid") {
            // The split runtime applies PID filtering in the daemon that owns
            // the USB function; this client cannot forward the list through
            // the shared TUNE API.  Do not silently accept it.
            args.error =
                "PID filtering is selected by sianod in the split runtime; "
                "pass -p/--pid to sianod";
            return args;
        } else {
            args.error = "unknown argument";
            return args;
        }
    }
    if (args.help || args.version) { args.valid = true; return args; }
    if (args.have_channel && args.have_frequency) {
        args.error = "--channel and --freq cannot be combined";
        return args;
    }
    if (!args.control && args.have_channel == args.have_frequency) {
        args.error = "exactly one of --channel or --freq is required";
        return args;
    }
    if (args.instance.empty()) { args.error = "--instance is required"; return args; }
    if (args.control && (args.duration_set || args.packet_count_set)) {
        args.error = "--control cannot be combined with --time or --packet-count";
        return args;
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

enum class ControlKind { none, channel, tune, quit };

struct ControlState final {
    std::mutex mutex;
    std::deque<std::pair<ControlKind, std::uint32_t>> commands;
    std::atomic<bool> interrupt{false};
    std::atomic<bool> stop_reader{false};
};

class ControlSignal final : public Px4TsSignal {
public:
    explicit ControlSignal(ControlState& state) noexcept : state_(state) {}
    bool stop_requested() const noexcept override
    {
        return state_.interrupt.load() || platform_stop_requested();
    }

private:
    ControlState& state_;
};

void post_command(ControlState& state, ControlKind kind, std::uint32_t value) noexcept
{
    std::lock_guard<std::mutex> lock(state.mutex);
    state.commands.emplace_back(kind, value);
    state.interrupt.store(true);
}

ControlKind take_command(ControlState& state, std::uint32_t& value) noexcept
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.commands.empty()) return ControlKind::none;
    const ControlKind kind = state.commands.front().first;
    value = state.commands.front().second;
    state.commands.pop_front();
    state.interrupt.store(!state.commands.empty());
    return kind;
}

// Reuses the accepted stdin parser/input helpers.  It does not touch the
// device; every selection is executed later through the shared UDTV runner.
void control_reader(ControlState* state) noexcept
{
    siano_control_input input{};
#ifdef _WIN32
    const auto input_handle = ::GetStdHandle(STD_INPUT_HANDLE);
#else
    const int input_handle = STDIN_FILENO;
#endif
    if (siano_control_input_init(&input, input_handle) != 0) {
        post_command(*state, ControlKind::quit, 0U);
        return;
    }
    std::string line;
    std::uint8_t buffer[128];
    while (!state->stop_reader.load()) {
        const int ready = siano_control_input_ready(&input);
        if (ready < 0) {
            post_command(*state, ControlKind::quit, 0U);
            break;
        }
        if (ready == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        const std::int64_t count =
            siano_control_input_read(&input, buffer, sizeof(buffer));
        if (count < 0) {
            if (count == -EINTR || count == -EAGAIN) continue;
            post_command(*state, ControlKind::quit, 0U);
            break;
        }
        if (count == 0) {
            post_command(*state, ControlKind::quit, 0U);
            break;
        }
        for (std::int64_t index = 0; index < count; ++index) {
            const char character = static_cast<char>(buffer[index]);
            if (character == '\n') {
                std::uint32_t value = 0U;
                switch (parse_control_line(line.c_str(), &value)) {
                case CONTROL_CHANNEL:
                    post_command(*state, ControlKind::channel, value);
                    break;
                case CONTROL_TUNE:
                    post_command(*state, ControlKind::tune, value);
                    break;
                case CONTROL_QUIT:
                    post_command(*state, ControlKind::quit, 0U);
                    break;
                case CONTROL_INVALID:
                    std::fprintf(stderr, "control: invalid\n");
                    break;
                case CONTROL_EMPTY:
                    break;
                }
                line.clear();
            } else if (character != '\r') {
                if (line.size() < 120U) line.push_back(character);
                else line.clear();
            }
        }
    }
}

bool resolve_control_hz(ControlKind kind, std::uint32_t value,
                        std::uint64_t& frequency_hz) noexcept
{
    dtv::tuning::Request request{};
    std::string channel;
    if (kind == ControlKind::channel) {
        channel = std::to_string(value);
        request.channel = std::string_view(channel);
    } else if (kind == ControlKind::tune) {
        if (value == 0U) return false;
        request.frequency_hz = value;
        request.system = dtv::userland::wire::System::isdb_t;
    } else {
        return false;
    }
    const auto resolved = dtv::tuning::resolve(request);
    if (!resolved ||
        resolved.value().system != dtv::userland::wire::System::isdb_t)
        return false;
    frequency_hz = resolved.value().frequency_hz;
    return true;
}

Px4TsArguments control_arguments(const SianoArgs& args, std::uint64_t frequency_hz) noexcept
{
    Px4TsArguments arguments{};
    arguments.valid = true;
    arguments.instance = args.instance;
    arguments.runtime_directory = args.runtime_directory.empty()
                                      ? default_runtime_directory()
                                      : args.runtime_directory;
    arguments.output = args.output;
    arguments.receiver = static_cast<std::uint8_t>(args.receiver);
    arguments.system = ipc::System::ISDB_T;
    arguments.frequency_hz = frequency_hz;
    arguments.tune_timeout_ms = static_cast<std::uint32_t>(args.tune_timeout_ms);
    return arguments;
}

int run_control(const SianoArgs& args, Px4TsOutput& output, Px4TsClock& clock) noexcept
{
    ControlState state;
    std::thread reader(control_reader, &state);
    ControlSignal signal(state);

    std::uint64_t frequency_hz = 0U;
    bool have_selection = false;
    if (args.have_channel) {
        dtv::tuning::Request initial{};
        initial.channel = std::string_view(args.channel);
        const auto resolved = dtv::tuning::resolve(initial);
        if (resolved &&
            resolved.value().system == dtv::userland::wire::System::isdb_t) {
            frequency_hz = resolved.value().frequency_hz;
            have_selection = true;
        }
    } else if (args.have_frequency) {
        if (args.frequency_hz != 0U &&
            args.frequency_hz <= std::numeric_limits<std::uint32_t>::max()) {
            frequency_hz = args.frequency_hz;
            have_selection = true;
        }
    }
    if ((args.have_channel || args.have_frequency) && !have_selection) {
        std::fprintf(stderr, "control: invalid initial selection\n");
        state.stop_reader.store(true);
        reader.join();
        return 2;
    }

    for (;;) {
        if (!have_selection) {
            while (!state.interrupt.load() && !platform_stop_requested())
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (platform_stop_requested()) break;
            std::uint32_t value = 0U;
            const ControlKind kind = take_command(state, value);
            if (kind == ControlKind::quit) break;
            if (kind == ControlKind::none) continue;
            if (!resolve_control_hz(kind, value, frequency_hz)) {
                std::fprintf(stderr, "control: invalid\n");
                continue;
            }
            have_selection = true;
        }

        // A command may have arrived before this session started; apply it
        // instead of dropping it.
        std::uint32_t pending_value = 0U;
        const ControlKind pending = take_command(state, pending_value);
        if (pending == ControlKind::quit) break;
        if (pending != ControlKind::none) {
            if (!resolve_control_hz(pending, pending_value, frequency_hz)) {
                std::fprintf(stderr, "control: invalid\n");
                have_selection = false;
                continue;
            }
        }

        Px4TsFailureKind failure_kind = Px4TsFailureKind::ipc;
        Px4TsRunDiagnostics diagnostics;
        const auto result = Px4TsRunner::run_wire(control_arguments(args, frequency_hz),
                                                  output, clock, signal,
                                                  &failure_kind, &diagnostics);
        if (!result) {
            // Preserve the old control mode's ability to accept another tune,
            // but never turn a failed UDTV operation into a silent success.
            std::fprintf(stderr, "control: %s\n", error_string(result.error()));
            if (platform_stop_requested()) break;
            std::uint32_t retry_value = 0U;
            const ControlKind retry = take_command(state, retry_value);
            if (retry == ControlKind::quit) break;
            if (retry != ControlKind::none &&
                resolve_control_hz(retry, retry_value, frequency_hz)) {
                continue;
            }
            have_selection = false;
            continue;
        }
        if (platform_stop_requested()) break;

        std::uint32_t next_value = 0U;
        const ControlKind next = take_command(state, next_value);
        if (next == ControlKind::quit) break;
        if (next == ControlKind::none) break;
        if (!resolve_control_hz(next, next_value, frequency_hz)) {
            std::fprintf(stderr, "control: invalid\n");
            have_selection = false;
            continue;
        }
    }
    state.stop_reader.store(true);
    reader.join();
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    auto owned_argv = windows_argv_utf8(argc, argv);
    std::vector<char*> argv_views;
    for (auto& argument : owned_argv) argv_views.push_back(argument.data());
    if (argv_views.empty()) return 2;
    argc = static_cast<int>(argv_views.size());
    argv = argv_views.data();
#endif
    bool control_mode = false;
    bool compatibility_flag = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index] != nullptr ? argv[index] : "");
        if (option == "--control") control_mode = true;
        if (option == "-h" || option == "--help" || option == "-V" ||
            option == "--version") compatibility_flag = true;
    }
    if (control_mode || compatibility_flag) {
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
        if (args.have_system && args.system == "isdb-s") {
            std::fprintf(stderr, "siano-ts: unsupported broadcast system (ISDB-T only)\n");
            return 3;
        }
        if (!install_signals()) {
            std::fprintf(stderr, "signal setup failed\n");
            return 70;
        }
        auto control_output = open_output(args.output);
        if (!control_output) {
            std::fprintf(stderr, "output setup failed\n");
            return 70;
        }
        PosixClock control_clock;
        const int status = run_control(args, *control_output, control_clock);
        const auto finished = finish_output(*control_output);
        return !finished && status == 0 ? 70 : status;
    }

    static const TsClientProfile profile{
        "siano-ts", "siano-userland", dtv::userland::identity::Product::siano,
        false, false, nullptr, true};
    Px4TsArguments arguments = parse_px4_ts_product_arguments(
        argc, const_cast<const char* const*>(argv), profile);
    if (!arguments.valid) {
        std::fprintf(stderr, "argument error: %s\n", arguments.error.c_str());
        usage();
        return 2;
    }
    if (arguments.help) { usage(); return 0; }
    if (arguments.version) {
        std::fprintf(stdout, "siano-ts %s (development; not a v1 release)\n",
                     SIANO_RUNTIME_VERSION);
        return 0;
    }
    if (arguments.instance.empty() || arguments.receiver >= 8U) {
        std::fprintf(stderr, "argument error: --instance is required and receiver must be 0..7\n");
        usage();
        return 2;
    }
    if (arguments.system != ipc::System::ISDB_T) {
        std::fprintf(stderr, "siano-ts: unsupported broadcast system (ISDB-T only)\n");
        return 3;
    }
    if (arguments.runtime_directory.empty())
        arguments.runtime_directory = default_runtime_directory();

    if (!install_signals()) {
        std::fprintf(stderr, "signal setup failed\n");
        return 70;
    }
    auto output = open_output(arguments.output);
    if (!output) {
        std::fprintf(stderr, "output setup failed\n");
        return 70;
    }
    PosixClock clock;
    PosixSignal signal;
    Px4TsFailureKind failure_kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics;
    const auto result = Px4TsRunner::run_wire(arguments, profile, *output, clock, signal,
                                              &failure_kind, &diagnostics);
    const auto finished = finish_output(*output);
    if (!result) {
        std::fprintf(stderr, "siano-ts: %s\n", error_string(result.error()));
        std::fprintf(stderr, "written packets=%llu bytes=%llu\n",
                     static_cast<unsigned long long>(diagnostics.packets_written),
                     static_cast<unsigned long long>(diagnostics.packets_written * 188U));
        return px4_ts_exit_status(result.error(), failure_kind);
    }
    return finished ? 0 : 70;
}
