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
#include <vector>

#ifndef SIANO_RUNTIME_VERSION
#define SIANO_RUNTIME_VERSION "0.0.0-dev"
#endif

namespace {

using namespace px4::userland;
using namespace px4::userland::cli;

const TsClientProfile siano_profile{
    "siano-ts", "siano-userland", dtv::userland::identity::Product::siano,
    false, false, nullptr, true};

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

enum class OutputOpenFailure {
    none,
    invalid_path,
    stdout_unavailable,
    open_failed,
    allocate_failed,
    not_ready,
};

struct OutputOpenDiagnostic final {
    OutputOpenFailure failure = OutputOpenFailure::none;
    unsigned long native_error = 0UL;
};

std::unique_ptr<Px4TsOutput> open_output(
    const std::string& path, OutputOpenDiagnostic* diagnostic = nullptr) noexcept
{
    if (diagnostic != nullptr) *diagnostic = OutputOpenDiagnostic{};
    const bool stdout_output = path == "-";
#ifdef _WIN32
    HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (!stdout_output) {
        const auto wide = utf8_to_wide_path(path);
        if (wide.empty()) {
            if (diagnostic != nullptr)
                diagnostic->failure = OutputOpenFailure::invalid_path;
            return {};
        }
        handle = ::CreateFileW(wide.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE && diagnostic != nullptr) {
            diagnostic->failure = OutputOpenFailure::open_failed;
            diagnostic->native_error = ::GetLastError();
        }
    }
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        if (diagnostic != nullptr && diagnostic->failure == OutputOpenFailure::none) {
            diagnostic->failure = stdout_output ? OutputOpenFailure::stdout_unavailable
                                                : OutputOpenFailure::open_failed;
            if (!stdout_output) diagnostic->native_error = ::GetLastError();
        }
        return {};
    }
    auto created = Px4TsWindowsOutput::create(handle, !stdout_output);
    if (!created) {
        if (diagnostic != nullptr)
            diagnostic->failure = OutputOpenFailure::allocate_failed;
        return {};
    }
    return std::move(created.value());
#else
    const int fd = stdout_output ? STDOUT_FILENO :
        ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        if (diagnostic != nullptr) {
            diagnostic->failure = OutputOpenFailure::open_failed;
            diagnostic->native_error = static_cast<unsigned long>(errno);
        }
        return {};
    }
    std::unique_ptr<Px4TsFdOutput> output(new (std::nothrow) Px4TsFdOutput(fd, !stdout_output));
    if (!output) {
        if (!stdout_output) ::close(fd);
        if (diagnostic != nullptr)
            diagnostic->failure = OutputOpenFailure::allocate_failed;
        return {};
    }
    if (!output->ready()) {
        if (diagnostic != nullptr) {
            diagnostic->failure = OutputOpenFailure::not_ready;
            diagnostic->native_error = static_cast<unsigned long>(errno);
        }
        return {};
    }
    return output;
#endif
}

void report_output_open_failure(const OutputOpenDiagnostic& diagnostic) noexcept
{
    if (diagnostic.failure == OutputOpenFailure::invalid_path) {
        std::fprintf(stderr, "--output is empty or not valid UTF-8 for a Windows path; use a valid writable file or '-' for stdout\n");
    } else if (diagnostic.failure == OutputOpenFailure::stdout_unavailable) {
        std::fprintf(stderr, "stdout is unavailable; choose a writable --output file\n");
    } else if (diagnostic.failure == OutputOpenFailure::open_failed) {
#ifdef _WIN32
        std::fprintf(stderr, "--output could not be opened (Windows error %lu); choose a writable file or '-' for stdout\n",
                     diagnostic.native_error);
#else
        std::fprintf(stderr, "--output could not be opened (errno %lu: %s); choose a writable file or '-' for stdout\n",
                     diagnostic.native_error,
                     std::strerror(static_cast<int>(diagnostic.native_error)));
#endif
    } else if (diagnostic.failure == OutputOpenFailure::allocate_failed) {
        std::fprintf(stderr, "--output writer could not be started; check available system resources\n");
    } else if (diagnostic.failure == OutputOpenFailure::not_ready) {
        std::fprintf(stderr, "--output is not ready for writing; check the file or use '-' for stdout\n");
    }
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

void usage(FILE* output = stderr) noexcept
{
    std::fprintf(output,
        "usage: siano-ts (-c CHANNEL | -f HZ) [--system ISDB-T] --instance TOKEN\n"
        "                [-t SECONDS|-] [-o FILE|-] [--runtime-dir DIR]\n"
        "                --receiver N [--tune-timeout-ms MS] [--packet-count N]\n"
        "                [--control]\n"
        "  -c, --channel TEXT     ISDB-T channel (13..62/T13..T62)\n"
        "  -f, --freq HZ          exact requested Hz (1..2^53-1)\n"
        "      --system SYS       ISDB-T (optional, case-insensitive)\n"
        "  -t, --time SECONDS     capture duration; '-' is unlimited\n"
        "  -o, --output FILE      writable output file; '-' is stdout\n"
        "      --control          accept channel N (13..62), tune HZ\n"
        "                         (1..4294967295), or quit on stdin; re-select\n"
        "                         through the shared UDTV API\n"
        "      --runtime-dir DIR  root for DIR/siano-userland/v1/TOKEN/<endpoint>;\n"
        "                         full path must fit this OS build's socket field\n"
        "      --instance TOKEN   required; 1..64 ASCII bytes, starts with a\n"
        "                         letter/digit, then letter/digit/_/./-; no\n"
        "                         trailing dot or reserved Windows device name\n"
        "      --receiver N       required; index 0..7; this enclosure has receiver 0 only\n"
        "      --tune-timeout-ms MS tune timeout (100..30000 ms)\n"
        "      --packet-count N   stop after N written 188-byte packets (N > 0)\n"
        "  -h, --help             print this help\n"
        "      --version          print the development version\n");
}

bool takes_shared_value(std::string_view option) noexcept
{
    return option == "-d" || option == "--device" || option == "--instance" ||
           option == "--runtime-dir" || option == "--receiver" || option == "-c" ||
           option == "--channel" || option == "-f" || option == "--freq" ||
           option == "-s" || option == "--system" || option == "--slot" ||
           option == "--tsid" || option == "-t" || option == "--time" ||
           option == "-o" || option == "--output" || option == "--packet-count" ||
           option == "--tune-timeout-ms";
}

struct ParsedArguments final {
    bool control = false;
    Px4TsArguments arguments;
};

ParsedArguments parse_arguments(int argc, char* const* argv) noexcept
{
    ParsedArguments parsed;
    std::vector<const char*> control_argv;
    control_argv.reserve(static_cast<std::size_t>(argc));
    control_argv.push_back(argv[0]);
    std::size_t control_options = 0U;
    bool unsupported_control_option = false;
    bool pid_option = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index] != nullptr ? argv[index] : "");
        if (option == "--control") {
            parsed.control = true;
            ++control_options;
            if (control_options == 1U) continue;
        }
        if (option == "-d" || option == "--device" || option == "--group" ||
            option == "--slot" || option == "--tsid")
            unsupported_control_option = true;
        if (option == "-p" || option == "--pid") {
            unsupported_control_option = true;
            pid_option = true;
        }
        control_argv.push_back(argv[index]);
        if (takes_shared_value(option) && index + 1 < argc) {
            const char* value = argv[++index];
            if ((option == "-o" || option == "--output") && value != nullptr &&
                std::string_view(value) == "--control") {
                control_argv.push_back("./--control");
            } else {
                control_argv.push_back(value);
            }
        }
    }
    if (!parsed.control) {
        parsed.arguments = parse_px4_ts_product_arguments(
            argc, const_cast<const char* const*>(argv), siano_profile);
        return parsed;
    }
    parsed.arguments = parse_px4_ts_control_arguments(
        static_cast<int>(control_argv.size()), control_argv.data(), siano_profile);
    if (parsed.arguments.valid && unsupported_control_option) {
        parsed.arguments.valid = false;
        parsed.arguments.error = pid_option
            ? "PID filtering is selected by sianod in the split runtime; pass -p/--pid to sianod"
            : "unknown argument";
    }
    if (parsed.arguments.valid &&
        (parsed.arguments.time_explicit || parsed.arguments.packet_count_set)) {
        parsed.arguments.valid = false;
        parsed.arguments.error = "--control cannot be combined with --time or --packet-count";
    }
    if (parsed.arguments.valid && !parsed.arguments.help &&
        !parsed.arguments.version && parsed.arguments.instance.empty()) {
        parsed.arguments.valid = false;
        parsed.arguments.error = "--instance is required";
    }
    if (parsed.arguments.valid && !parsed.arguments.help &&
        !parsed.arguments.version && parsed.arguments.receiver >= 8U) {
        parsed.arguments.valid = false;
        parsed.arguments.error = "--receiver must be an integer in 0..7";
    }
    return parsed;
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

void report_control_input_error() noexcept
{
    std::fprintf(stderr,
                 "control: use 'channel N' (13..62), 'tune HZ' (1..4294967295), or 'quit'\n");
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
    bool line_too_long_reported = false;
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
                    report_control_input_error();
                    break;
                case CONTROL_EMPTY:
                    break;
                }
                line.clear();
                line_too_long_reported = false;
            } else if (character != '\r') {
                if (line.size() < 120U) line.push_back(character);
                else {
                    if (!line_too_long_reported) {
                        std::fprintf(stderr, "control: input line exceeds 120 bytes; send one supported command per line\n");
                        line_too_long_reported = true;
                    }
                    line.clear();
                }
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

Px4TsArguments control_arguments(const Px4TsArguments& args,
                                 std::uint64_t frequency_hz) noexcept
{
    Px4TsArguments arguments = args;
    arguments.frequency_hz = frequency_hz;
    arguments.tuning_explicit = true;
    return arguments;
}

struct ControlRunSummary {
    std::uint64_t frequency_hz;
    std::uint64_t packets_written;
    bool succeeded;
};

int run_control(const Px4TsArguments& args, Px4TsOutput& output, Px4TsClock& clock,
                std::vector<ControlRunSummary>& summaries) noexcept
{
    ControlState state;
    std::thread reader(control_reader, &state);
    ControlSignal signal(state);

    std::uint64_t frequency_hz = args.frequency_hz;
    bool have_selection = args.tuning_explicit;

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
                report_control_input_error();
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
                report_control_input_error();
                have_selection = false;
                continue;
            }
        }

        Px4TsFailureKind failure_kind = Px4TsFailureKind::ipc;
        Px4TsRunDiagnostics diagnostics;
        const auto result = Px4TsRunner::run_wire(control_arguments(args, frequency_hz),
                                                  siano_profile, output, clock, signal,
                                                  &failure_kind, &diagnostics);
        summaries.push_back({frequency_hz, diagnostics.packets_written,
                             static_cast<bool>(result)});
        if (!result) {
            // Preserve the old control mode's ability to accept another tune,
            // but never turn a failed UDTV operation into a silent success.
            if (diagnostics.stage == Px4TsDiagnosticStage::none) {
                std::fprintf(stderr, "control: %s\n", error_string(result.error()));
            } else {
                print_px4_ts_run_diagnostic(stderr, "control", result.error(), diagnostics);
            }
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
            report_control_input_error();
            have_selection = false;
            continue;
        }
    }
    state.stop_reader.store(true);
    reader.join();
    return 0;
}

void report_control_summaries(const std::vector<ControlRunSummary>& summaries,
                              bool output_finished) noexcept
{
    for (std::size_t index = 0U; index < summaries.size(); ++index) {
        const auto& summary = summaries[index];
        const bool complete = summary.succeeded && output_finished;
        if (complete) {
            std::fprintf(stderr,
                         "control: selection=%zu frequency_hz=%llu packets=%llu bytes=%llu status=complete\n",
                         index + 1U,
                         static_cast<unsigned long long>(summary.frequency_hz),
                         static_cast<unsigned long long>(summary.packets_written),
                         static_cast<unsigned long long>(summary.packets_written * 188U));
        } else {
            std::fprintf(stderr,
                         "control: selection=%zu frequency_hz=%llu packets=%llu bytes=unknown status=%s\n",
                         index + 1U,
                         static_cast<unsigned long long>(summary.frequency_hz),
                         static_cast<unsigned long long>(summary.packets_written),
                         summary.succeeded ? "output-error" : "run-error");
        }
    }
}

int control_exit_status(int status, bool output_finished) noexcept
{
    return !output_finished && status == 0 ? 70 : status;
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
    const ParsedArguments parsed = parse_arguments(argc, argv);
    const Px4TsArguments& arguments = parsed.arguments;
    if (arguments.help) { usage(stdout); return 0; }
    if (arguments.version) {
        std::fprintf(stdout, "siano-ts %s (development; not a v1 release)\n",
                     SIANO_RUNTIME_VERSION);
        return 0;
    }
    if (!arguments.valid) {
        std::fprintf(stderr, "argument error: %s\n", arguments.error.c_str());
        usage();
        return 2;
    }
    if (parsed.control) {
        if (arguments.system == ipc::System::ISDB_S) {
            std::fprintf(stderr, "siano-ts: unsupported broadcast system (ISDB-T only)\n");
            return 3;
        }
        if (arguments.tuning_explicit &&
            (arguments.frequency_hz == 0U ||
             arguments.frequency_hz > std::numeric_limits<std::uint32_t>::max())) {
            std::fprintf(stderr,
                         "argument error: --freq is outside the Siano tuner range (1..4294967295 Hz)\n");
            return 2;
        }
        if (!install_signals()) {
            std::fprintf(stderr, "signal setup failed\n");
            return 70;
        }
        OutputOpenDiagnostic control_output_diagnostic{};
        auto control_output = open_output(arguments.output, &control_output_diagnostic);
        if (!control_output) {
            report_output_open_failure(control_output_diagnostic);
            return 70;
        }
        PosixClock control_clock;
        std::vector<ControlRunSummary> summaries;
        const int status = run_control(arguments, *control_output, control_clock, summaries);
        const auto finished = finish_output(*control_output);
        report_control_summaries(summaries, static_cast<bool>(finished));
        return control_exit_status(status, static_cast<bool>(finished));
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
    if (!install_signals()) {
        std::fprintf(stderr, "signal setup failed\n");
        return 70;
    }
    OutputOpenDiagnostic output_diagnostic{};
    auto output = open_output(arguments.output, &output_diagnostic);
    if (!output) {
        report_output_open_failure(output_diagnostic);
        return 70;
    }
    PosixClock clock;
    PosixSignal signal;
    Px4TsFailureKind failure_kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics;
    const auto result = Px4TsRunner::run_wire(arguments, siano_profile, *output, clock, signal,
                                              &failure_kind, &diagnostics);
    const auto finished = finish_output(*output);
    if (!result) {
        if (diagnostics.stage == Px4TsDiagnosticStage::none) {
            std::fprintf(stderr, "siano-ts: %s\n", error_string(result.error()));
        } else {
            print_px4_ts_run_diagnostic(stderr, "siano-ts", result.error(), diagnostics);
        }
        std::fprintf(stderr, "written packets=%llu bytes=%llu\n",
                     static_cast<unsigned long long>(diagnostics.packets_written),
                     static_cast<unsigned long long>(diagnostics.packets_written * 188U));
        return px4_ts_exit_status(result.error(), failure_kind);
    }
    return finished ? 0 : 70;
}
