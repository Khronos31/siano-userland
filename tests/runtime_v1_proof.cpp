/* SPDX-License-Identifier: GPL-2.0-or-later */
// Limited acceptance proof for the Siano public runtime.  The real assembly
// (same function as sianod) is served over the real AF_UNIX server, and the
// real siano-ts client runs as a separate process.  Only the bottom USB layer
// is replaced by the loopback fixture.
#include "siano-enclosure.h"

#include "px4/posix_ipc.h"
#include "px4/posix_tuner_nonce.h"

extern "C" {
#include "output-writer.h"
#include "protocol.h"
}

#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifndef SIANO_TS_PATH
#error "SIANO_TS_PATH must name the built siano-ts client"
#endif
#ifndef SIANOCTL_PATH
#error "SIANOCTL_PATH must name the built sianoctl client"
#endif
#ifndef SIANO_RUNTIME_VERSION
#define SIANO_RUNTIME_VERSION "0.0.0-dev"
#endif

extern "C" int test_libusb_deliver(struct libusb_transfer*, const std::uint8_t*,
                                   std::size_t);
extern "C" int test_libusb_pending(struct libusb_transfer*);
extern "C" unsigned test_libusb_tune_requests(void);
extern "C" std::uint32_t test_libusb_last_tune_hz(void);
extern "C" unsigned test_libusb_control_requests(void);
extern "C" int test_libusb_deliver_control_to(struct libusb_transfer*, std::uint16_t);

namespace {

using namespace dtv::userland;
using namespace dtv::userland::detail;
using namespace siano::runtime;
namespace posix = px4::userland::ipc::posix;

constexpr std::size_t kPackets = 5U;

bool deliver_ts(siano_device& device, std::uint8_t marker, std::size_t packets)
{
    std::vector<std::uint8_t> payload(packets * SIANO_OUTPUT_PACKET);
    for (std::size_t i = 0; i < packets; ++i) {
        auto* packet = payload.data() + i * SIANO_OUTPUT_PACKET;
        std::memset(packet, marker, SIANO_OUTPUT_PACKET);
        packet[0] = 0x47U;
        packet[1] = 0x01U;
        packet[2] = static_cast<std::uint8_t>(marker + i);
        packet[3] = 0x10U;
    }
    std::vector<std::uint8_t> message(SMS_HEADER_SIZE + payload.size());
    sms_pack_header(message.data(), MSG_SMS_DVBT_BDA_DATA, 0U, 0U,
                    static_cast<std::uint16_t>(message.size()), 0U);
    std::memcpy(message.data() + SMS_HEADER_SIZE, payload.data(), payload.size());
    for (std::size_t i = 0U; i < MAX_URBS; ++i) {
        if (device.transfers[i] && test_libusb_pending(device.transfers[i]))
            return test_libusb_deliver(device.transfers[i], message.data(), message.size()) == 0;
    }
    return false;
}

bool wait_transfer_pending(struct libusb_transfer* transfer)
{
    for (unsigned i = 0U; i < 3000U; ++i) {
        if (test_libusb_pending(transfer)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::vector<std::uint8_t> expected_ts(std::uint8_t marker, std::size_t packets)
{
    std::vector<std::uint8_t> expected(packets * SIANO_OUTPUT_PACKET);
    for (std::size_t i = 0; i < packets; ++i) {
        auto* packet = expected.data() + i * SIANO_OUTPUT_PACKET;
        std::memset(packet, marker, SIANO_OUTPUT_PACKET);
        packet[0] = 0x47U;
        packet[1] = 0x01U;
        packet[2] = static_cast<std::uint8_t>(marker + i);
        packet[3] = 0x10U;
    }
    return expected;
}

std::vector<std::uint8_t> read_file(const std::string& path)
{
    std::vector<std::uint8_t> bytes;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return bytes;
    std::uint8_t buffer[4096];
    std::size_t got = 0U;
    while ((got = std::fread(buffer, 1U, sizeof(buffer), file)) > 0U)
        bytes.insert(bytes.end(), buffer, buffer + got);
    std::fclose(file);
    return bytes;
}

int run_client(const std::string& runtime_dir, const std::string& instance,
               const std::string& output, const char* time_or_count,
               const char* time_value, bool explicit_runtime_dir = true)
{
    pid_t pid = fork();
    if (pid == 0) {
        const char* argv[16];
        int index = 0;
        argv[index++] = SIANO_TS_PATH;
        if (std::strcmp(time_or_count, "time") == 0) {
            argv[index++] = "-c";
            argv[index++] = "T13";
            argv[index++] = "-t";
            argv[index++] = time_value;
        } else {
            argv[index++] = "-c";
            argv[index++] = "T13";
            argv[index++] = "--packet-count";
            argv[index++] = time_value;
        }
        argv[index++] = "--instance";
        argv[index++] = instance.c_str();
        if (explicit_runtime_dir) {
            argv[index++] = "--runtime-dir";
            argv[index++] = runtime_dir.c_str();
        }
        argv[index++] = "-o";
        argv[index++] = output.c_str();
        argv[index++] = "--tune-timeout-ms";
        argv[index++] = "5000";
        argv[index] = nullptr;
        execv(SIANO_TS_PATH, const_cast<char* const*>(argv));
        _exit(127);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int run_control(const std::string& runtime_dir, const std::string& instance,
                std::string& output, const char* command,
                bool explicit_runtime_dir = true)
{
    std::fprintf(stderr, "PROOF control_argv=%s --instance %s%s %s\n",
                 SIANOCTL_PATH, instance.c_str(),
                 explicit_runtime_dir ? " --runtime-dir <explicit>" : "", command);
    int output_pipe[2];
    if (pipe(output_pipe) != 0) return -1;
    const pid_t pid = fork();
    if (pid == 0) {
        close(output_pipe[0]);
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0) _exit(127);
        close(output_pipe[1]);
        const char* argv_with_runtime[] = {SIANOCTL_PATH, "--instance", instance.c_str(),
                              "--runtime-dir", runtime_dir.c_str(), command, nullptr};
        const char* argv_default[] = {SIANOCTL_PATH, "--instance", instance.c_str(),
                              command, nullptr};
        execv(SIANOCTL_PATH, const_cast<char* const*>(explicit_runtime_dir
                    ? argv_with_runtime : argv_default));
        _exit(127);
    }
    close(output_pipe[1]);
    std::array<char, 4096U> buffer{};
    ssize_t count = 0;
    while ((count = read(output_pipe[0], buffer.data(), buffer.size())) > 0)
        output.append(buffer.data(), static_cast<std::size_t>(count));
    close(output_pipe[0]);
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

int run_control_ts(const std::string& runtime_dir, const std::string& instance,
                   const std::string& output, siano_device& device,
                   SianoFamily& family)
{
    int input_pipe[2];
    if (pipe(input_pipe) != 0) return -1;
    const pid_t pid = fork();
    if (pid == 0) {
        close(input_pipe[1]);
        if (dup2(input_pipe[0], STDIN_FILENO) < 0) _exit(127);
        close(input_pipe[0]);
        const char* argv[] = {SIANO_TS_PATH, "--control", "--channel", "T13",
                              "--instance", instance.c_str(), "--runtime-dir",
                              runtime_dir.c_str(), "-o", output.c_str(),
                              "--tune-timeout-ms", "5000", nullptr};
        execv(SIANO_TS_PATH, const_cast<char* const*>(argv));
        _exit(127);
    }
    close(input_pipe[0]);
    if (pid < 0) { close(input_pipe[1]); return -1; }

    bool ok = true;
    unsigned tunes = test_libusb_tune_requests();
    std::vector<std::uint8_t> expected;
    for (unsigned selection = 0U; selection < 2U; ++selection) {
        bool attached = false;
        const std::uint32_t expected_frequency =
            selection == 0U ? 473142857U : 479142857U;
        for (unsigned i = 0U; i < 5000U; ++i) {
            if (test_libusb_tune_requests() > tunes && family.stream_attached() &&
                test_libusb_last_tune_hz() == expected_frequency) {
                attached = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!attached) { ok = false; break; }
        for (std::size_t i = 0U; i < MAX_URBS; ++i) {
            if (!wait_transfer_pending(device.transfers[i]) ||
                test_libusb_deliver_control_to(device.transfers[i],
                                               MSG_SMS_SIGNAL_DETECTED_IND) != 0 ||
                !wait_transfer_pending(device.transfers[i])) ok = false;
        }
        const std::uint8_t marker = static_cast<std::uint8_t>(0x61U + selection * 8U);
        if (ok && !deliver_ts(device, marker, 2U)) ok = false;
        const auto packets = expected_ts(marker, 2U);
        expected.insert(expected.end(), packets.begin(), packets.end());
        const std::size_t expected_bytes = expected.size();
        for (unsigned i = 0U; i < 5000U && read_file(output).size() < expected_bytes; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (read_file(output).size() < expected_bytes) { ok = false; break; }
        tunes = test_libusb_tune_requests();
        const char* command = selection == 0U ? "tune 479142857\n" : "quit\n";
        const std::size_t length = std::strlen(command);
        if (!ok || write(input_pipe[1], command, length) !=
                       static_cast<ssize_t>(length)) {
            ok = false;
            break;
        }
    }
    close(input_pipe[1]);
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
    const auto actual = read_file(output);
    std::fprintf(stderr,
        "PROOF control_ts ok=%d exit=%d stdout_bytes=%zu expected_bytes=%zu tunes=%u hz=%u\n",
        ok, WEXITSTATUS(status), actual.size(), expected.size(),
        test_libusb_tune_requests(), test_libusb_last_tune_hz());
    if (!ok || WEXITSTATUS(status) != 0 || actual != expected ||
        test_libusb_tune_requests() < 2U ||
        test_libusb_last_tune_hz() != 479142857U) return 1;
    return 0;
}

int run_control_ts_failure(const std::string& runtime_dir,
                           const std::string& instance)
{
    int input_pipe[2];
    int error_pipe[2];
    if (pipe(input_pipe) != 0 || pipe(error_pipe) != 0) return -1;
    const pid_t pid = fork();
    if (pid == 0) {
        close(input_pipe[1]);
        close(error_pipe[0]);
        if (dup2(input_pipe[0], STDIN_FILENO) < 0 ||
            dup2(error_pipe[1], STDERR_FILENO) < 0) _exit(127);
        close(input_pipe[0]);
        close(error_pipe[1]);
        const char* argv[] = {SIANO_TS_PATH, "--control", "--channel", "T13",
                              "--instance", instance.c_str(), "--runtime-dir",
                              runtime_dir.c_str(), "-o", "/dev/null",
                              "--tune-timeout-ms", "5000", nullptr};
        execv(SIANO_TS_PATH, const_cast<char* const*>(argv));
        _exit(127);
    }
    close(input_pipe[0]);
    close(input_pipe[1]);
    close(error_pipe[1]);
    if (pid < 0) { close(error_pipe[0]); return -1; }
    std::string errors;
    std::array<char, 1024U> buffer{};
    ssize_t count = 0;
    while ((count = read(error_pipe[0], buffer.data(), buffer.size())) > 0)
        errors.append(buffer.data(), static_cast<std::size_t>(count));
    close(error_pipe[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
    std::fprintf(stderr, "PROOF control_ts_failure exit=%d stderr=%s",
                 WEXITSTATUS(status), errors.c_str());
    return WEXITSTATUS(status) == 0 && errors.find("control: NOT_READY") !=
        std::string::npos ? 0 : 1;
}

bool terminate_child_bounded(pid_t pid, int& status)
{
    kill(pid, SIGTERM);
    for (unsigned i = 0U; i < 1000U; ++i) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) return true;
        if (waited < 0 && errno != EINTR) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    kill(pid, SIGKILL);
    for (unsigned i = 0U; i < 1000U; ++i) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) return true;
        if (waited < 0 && errno != EINTR) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool prove_bounded_child_cleanup()
{
    struct sigaction ignored{};
    struct sigaction original{};
    ignored.sa_handler = SIG_IGN;
    sigemptyset(&ignored.sa_mask);
    if (sigaction(SIGTERM, &ignored, &original) != 0) return false;
    const pid_t pid = fork();
    if (pid != 0) sigaction(SIGTERM, &original, nullptr);
    if (pid == 0) {
        const rlimit address_space{512U * 1024U * 1024U, 512U * 1024U * 1024U};
        const rlimit cpu_time{10U, 10U};
        const rlimit file_size{1024U * 1024U, 1024U * 1024U};
        if (setrlimit(RLIMIT_AS, &address_space) != 0 ||
            setrlimit(RLIMIT_CPU, &cpu_time) != 0 ||
            setrlimit(RLIMIT_FSIZE, &file_size) != 0) _exit(126);
        for (;;) pause();
    }
    if (pid < 0) return false;
    int status = 0;
    const bool reaped = terminate_child_bounded(pid, status);
    const bool killed = reaped && WIFSIGNALED(status) &&
                        WTERMSIG(status) == SIGKILL;
    std::fprintf(stderr, "PROOF bounded_setup_cleanup reaped=%d sigkill=%d\n",
                 reaped, killed);
    return killed;
}

int run_control_ts_write_failure(const std::string& runtime_dir,
                                 const std::string& instance,
                                 siano_device& device, SianoFamily& family)
{
    const std::string output = runtime_dir + "/control-output-error.fifo";
    if (mkfifo(output.c_str(), 0600) != 0) return -1;
    int input_pipe[2];
    int error_pipe[2];
    if (pipe(input_pipe) != 0 || pipe(error_pipe) != 0) {
        unlink(output.c_str());
        return -1;
    }
    const pid_t pid = fork();
    if (pid == 0) {
        close(input_pipe[1]);
        close(error_pipe[0]);
        const rlimit address_space{512U * 1024U * 1024U, 512U * 1024U * 1024U};
        const rlimit cpu_time{10U, 10U};
        const rlimit file_size{1024U * 1024U, 1024U * 1024U};
        if (setrlimit(RLIMIT_AS, &address_space) != 0 ||
            setrlimit(RLIMIT_CPU, &cpu_time) != 0 ||
            setrlimit(RLIMIT_FSIZE, &file_size) != 0) _exit(126);
        if (dup2(input_pipe[0], STDIN_FILENO) < 0 ||
            dup2(error_pipe[1], STDERR_FILENO) < 0) _exit(127);
        close(input_pipe[0]);
        close(error_pipe[1]);
        const char* argv[] = {SIANO_TS_PATH, "--control", "--channel", "T13",
                              "--instance", instance.c_str(), "--runtime-dir",
                              runtime_dir.c_str(), "-o", output.c_str(),
                              "--tune-timeout-ms", "5000", nullptr};
        execv(SIANO_TS_PATH, const_cast<char* const*>(argv));
        _exit(127);
    }
    close(input_pipe[0]);
    close(error_pipe[1]);
    if (pid < 0) {
        close(input_pipe[1]);
        close(error_pipe[0]);
        unlink(output.c_str());
        return -1;
    }
    const int error_flags = fcntl(error_pipe[0], F_GETFL, 0);
    if (error_flags < 0 || fcntl(error_pipe[0], F_SETFL,
                                  error_flags | O_NONBLOCK) != 0) {
        close(input_pipe[1]);
        int status = 0;
        const bool reaped = terminate_child_bounded(pid, status);
        close(error_pipe[0]);
        unlink(output.c_str());
        return reaped ? -1 : -2;
    }
    const int output_reader = open(output.c_str(), O_RDONLY | O_NONBLOCK);
    if (output_reader < 0) {
        close(input_pipe[1]);
        int status = 0;
        const bool reaped = terminate_child_bounded(pid, status);
        close(error_pipe[0]);
        unlink(output.c_str());
        return reaped ? -1 : -2;
    }

    constexpr std::size_t max_stderr_bytes = 65536U;
    bool ok = false;
    const unsigned tunes = test_libusb_tune_requests();
    for (unsigned i = 0U; i < 5000U; ++i) {
        if (test_libusb_tune_requests() > tunes && family.stream_attached() &&
            test_libusb_last_tune_hz() == 473142857U) {
            ok = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (ok) {
        for (std::size_t i = 0U; i < MAX_URBS; ++i) {
            if (!wait_transfer_pending(device.transfers[i]) ||
                test_libusb_deliver_control_to(device.transfers[i],
                                               MSG_SMS_SIGNAL_DETECTED_IND) != 0 ||
                !wait_transfer_pending(device.transfers[i])) ok = false;
        }
    }
    close(output_reader);
    const std::uint8_t marker = 0x71U;
    if (ok && !deliver_ts(device, marker, 2U)) ok = false;
    std::string errors;
    std::array<char, 1024U> buffer{};
    if (ok) {
        for (unsigned i = 0U; i < 5000U; ++i) {
            const ssize_t count = read(error_pipe[0], buffer.data(), buffer.size());
            if (count > 0) {
                const auto bytes = static_cast<std::size_t>(count);
                if (errors.size() + bytes > max_stderr_bytes) {
                    ok = false;
                    break;
                }
                errors.append(buffer.data(), bytes);
            }
            else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                     errno != EINTR) {
                ok = false;
                break;
            }
            if (errors.find("control: INTERNAL") != std::string::npos) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (errors.find("control: INTERNAL") == std::string::npos) ok = false;
    }
    const char command[] = "quit\n";
    if (write(input_pipe[1], command, sizeof(command) - 1U) !=
        static_cast<ssize_t>(sizeof(command) - 1U)) ok = false;
    close(input_pipe[1]);

    int status = 0;
    bool exited = false;
    for (unsigned i = 0U; i < 5000U; ++i) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) { exited = true; break; }
        if (waited < 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!exited) {
        kill(pid, SIGTERM);
        for (unsigned i = 0U; i < 1000U; ++i) {
            const pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) { exited = true; break; }
            if (waited < 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!exited) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }
    for (unsigned i = 0U; i < 64U; ++i) {
        const ssize_t count = read(error_pipe[0], buffer.data(), buffer.size());
        if (count <= 0) break;
        const auto bytes = static_cast<std::size_t>(count);
        if (errors.size() + bytes > max_stderr_bytes) {
            ok = false;
            break;
        }
        errors.append(buffer.data(), bytes);
    }
    close(error_pipe[0]);
    unlink(output.c_str());
    const bool normal_exit = exited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    const bool failure_summary =
        errors.find("control: INTERNAL") != std::string::npos &&
        errors.find("packets=0 bytes=unknown status=run-error") != std::string::npos;
    std::fprintf(stderr,
        "PROOF control_ts_write_failure started=%d exited=%d exit=%d stats_match=%d stderr=%s",
        ok, exited, normal_exit ? WEXITSTATUS(status) : -1, failure_summary,
        errors.c_str());
    return ok && normal_exit && failure_summary ? 0 : 1;
}

bool check_status(std::string_view json, const std::string& instance,
                  const std::array<std::uint8_t, 16U>& epoch, const char* receiver_state)
{
    constexpr char hex[] = "0123456789abcdef";
    std::array<char, 33U> epoch_text{};
    for (std::size_t i = 0U; i < epoch.size(); ++i) {
        epoch_text[i * 2U] = hex[(epoch[i] >> 4U) & 15U];
        epoch_text[i * 2U + 1U] = hex[epoch[i] & 15U];
    }
    const std::string expected_epoch(epoch_text.data(), 32U);
    return json.size() > 2U && json.front() == '{' && json.back() == '\n' &&
           json.find("\"name\":\"userland.status\"") != std::string_view::npos &&
           json.find("\"product\":\"siano-userland\"") != std::string_view::npos &&
           json.find("\"version\":\"0.0.0-dev\"") != std::string_view::npos &&
           json.find("\"epoch\":\"" + expected_epoch + "\"") != std::string_view::npos &&
           json.find("\"instance\":\"" + instance + "\"") != std::string_view::npos &&
           json.find("\"descriptor\":{\"receiver\":0") != std::string_view::npos &&
           json.find("\"systems\":[\"ISDB-T\"]") != std::string_view::npos &&
           json.find(std::string("\"state\":\"") + receiver_state + "\"") !=
               std::string_view::npos &&
           json.find("\"measurements\":{}") != std::string_view::npos &&
           json.find("\"readers\":[]") != std::string_view::npos;
}

std::string make_private_dir()
{
    std::array<char, 64U> templ{};
    std::snprintf(templ.data(), templ.size(), "/tmp/siano-proof-XXXXXX");
    char* created = mkdtemp(templ.data());
    if (created == nullptr) return {};
    if (chmod(created, 0700) != 0) return {};
    return std::string(created);
}

SianoRuntimeConfig base_config()
{
    SianoRuntimeConfig config;
    config.usb = nullptr;
    config.own_usb = false;
    config.selector = siano_device_selector{};
    config.selector.kind = SIANO_DEVICE_SELECTOR_PORT;
    config.selector.bus = 1U;
    config.selector.ports[0] = 2U;
    config.selector.ports[1] = 3U;
    config.selector.port_count = 2U;
    config.selector_text = "usb:1-2.3";
    config.queue_packets = 64U;
    return config;
}

}  // namespace

int main()
{
    if (!prove_bounded_child_cleanup()) {
        std::fprintf(stderr, "PROOF bounded setup cleanup failed\n");
        return 1;
    }
    // 1. The published LIST is the adopted shape: one terrestrial receiver 0
    //    and an empty reader set (no external CARD).
    const std::string runtime_dir = make_private_dir();
    assert(!runtime_dir.empty());
    const std::string instance = "proofinst0001";
    const SianoRuntimeConfig config = base_config();

    posix::PosixTunerNonceSource epoch_source;
    const auto epoch = epoch_source.generate();
    assert(epoch);

    SianoRuntimeConfig group_config = config;
    group_config.access =
        dtv::userland::host_runtime::EndpointAccess::shared_group;
    AssemblyDiagnostic group_diagnostic{};
    const auto group_without_root = assemble_siano_context(
        group_config, "proofgroup0001", nullptr, epoch.value(), SIANO_RUNTIME_VERSION,
        &group_diagnostic);
    if (group_without_root || group_without_root.error() != Error::INVALID_ARGUMENT ||
        describe_assembly_diagnostic(group_diagnostic).find("--runtime-dir") ==
            std::string::npos) {
        std::fprintf(stderr, "PROOF groupRoot omitted root diagnostic mismatch\n");
        return 1;
    }

    const std::size_t socket_capacity = sizeof(sockaddr_un{}.sun_path);
    const std::string long_runtime = "/" + std::string(socket_capacity + 8U, 'r');
    AssemblyDiagnostic endpoint_diagnostic{};
    const auto overlong_endpoint = assemble_siano_context(
        config, "proofinst0001", long_runtime.c_str(), epoch.value(),
        SIANO_RUNTIME_VERSION, &endpoint_diagnostic);
    const std::string endpoint_detail = describe_assembly_diagnostic(endpoint_diagnostic);
    const std::string path_bytes = std::to_string(endpoint_diagnostic.endpoint.path_bytes);
    const std::string capacity_bytes = std::to_string(socket_capacity);
    if (overlong_endpoint || overlong_endpoint.error() != Error::INVALID_ARGUMENT ||
        endpoint_diagnostic.reason != AssemblyDiagnosticReason::endpoint_path ||
        endpoint_diagnostic.endpoint.reason !=
            dtv::userland::host_runtime::EndpointDiagnosticReason::path_too_long ||
        endpoint_diagnostic.endpoint.path_bytes + 1U !=
            endpoint_diagnostic.endpoint.required_bytes ||
        endpoint_diagnostic.endpoint.native_capacity != socket_capacity ||
        endpoint_detail.find(path_bytes) == std::string::npos ||
        endpoint_detail.find(capacity_bytes) == std::string::npos ||
        endpoint_detail.find("--runtime-dir") == std::string::npos ||
        endpoint_detail.find("--instance") == std::string::npos) {
        std::fprintf(stderr, "PROOF endpointPath diagnostic mismatch: %s\n",
                     endpoint_detail.c_str());
        return 1;
    }

    AssemblyDiagnostic instance_diagnostic{};
    const auto invalid_instance = assemble_siano_context(
        config, "bad instance", runtime_dir.c_str(), epoch.value(),
        SIANO_RUNTIME_VERSION, &instance_diagnostic);
    if (invalid_instance || invalid_instance.error() != Error::INVALID_ARGUMENT ||
        instance_diagnostic.reason != AssemblyDiagnosticReason::invalid_instance ||
        describe_assembly_diagnostic(instance_diagnostic).find("--instance") ==
            std::string::npos) {
        std::fprintf(stderr, "PROOF instance diagnostic mismatch\n");
        return 1;
    }

    const std::string missing_root = runtime_dir + "/missing-runtime";
    AssemblyDiagnostic missing_root_diagnostic{};
    const auto missing_root_result = assemble_siano_context(
        config, "proofinst0001", missing_root.c_str(), epoch.value(),
        SIANO_RUNTIME_VERSION, &missing_root_diagnostic);
    if (missing_root_result || missing_root_result.error() != Error::UNSUPPORTED ||
        missing_root_diagnostic.reason != AssemblyDiagnosticReason::listener ||
        missing_root_diagnostic.listener.reason !=
            dtv::userland::host_listener_context::ListenerDiagnosticReason::runtime_directory_missing ||
        describe_assembly_diagnostic(missing_root_diagnostic).find("--runtime-dir") ==
            std::string::npos) {
        std::fprintf(stderr, "PROOF missing runtime diagnostic mismatch\n");
        return 1;
    }

    const std::string unsafe_root = make_private_dir();
    assert(!unsafe_root.empty());
    if (chmod(unsafe_root.c_str(), 0777) != 0) {
        std::fprintf(stderr, "PROOF could not set isolated unsafe mode fixture\n");
        (void)rmdir(unsafe_root.c_str());
        return 1;
    }
    AssemblyDiagnostic mode_diagnostic{};
    const auto unsafe_root_result = assemble_siano_context(
        config, "proofinst0001", unsafe_root.c_str(), epoch.value(),
        SIANO_RUNTIME_VERSION, &mode_diagnostic);
    const std::string mode_detail = describe_assembly_diagnostic(mode_diagnostic);
    (void)chmod(unsafe_root.c_str(), 0700);
    (void)rmdir(unsafe_root.c_str());
    if (unsafe_root_result || unsafe_root_result.error() != Error::UNSUPPORTED ||
        mode_diagnostic.reason != AssemblyDiagnosticReason::listener ||
        mode_diagnostic.listener.reason !=
            dtv::userland::host_listener_context::ListenerDiagnosticReason::runtime_directory_mode ||
        mode_detail.find("group- or other-writable") == std::string::npos) {
        std::fprintf(stderr, "PROOF runtime mode diagnostic mismatch: %s\n",
                     mode_detail.c_str());
        return 1;
    }

    auto assembly = assemble_siano_context(config, instance, runtime_dir.c_str(),
                                           epoch.value(), SIANO_RUNTIME_VERSION);
    if (!assembly) {
        std::fprintf(stderr, "assemble_error=%s\n", error_string(assembly.error()));
        return 1;
    }
    dtv::userland::host_listener_context::EnclosureContext& owned =
        *assembly.value().context;
    SianoFamily* family = assembly.value().family;
    assert(family != nullptr);
    siano_device& device = family->adapter().device();

    wire::ListResponse control{};
    const auto facts_result = resolve_rio_facts(nullptr, config.selector, config.selector_text);
    assert(facts_result);
    assert(build_siano_control_context(facts_result.value(), epoch.value(),
                                       SIANO_RUNTIME_VERSION, control));
    assert(control.readers.present && control.readers.value.count == 0U);
    assert(control.receivers.present && control.receivers.value.count == 1U);
    assert(control.receivers.value.items[0].systems.mask == 1U);
    std::fprintf(stderr, "PROOF list readers=%u receivers=%u\n",
                 static_cast<unsigned>(control.readers.value.count),
                 static_cast<unsigned>(control.receivers.value.count));

    std::atomic<bool> serving{true};
    std::thread server([&]() {
        while (serving.load()) {
            const auto polled = owned.poll_once(Timeout{50U});
            if (!polled) {
                std::fprintf(stderr, "PROOF server poll error=%u\n",
                             static_cast<unsigned>(polled.error()));
                break;
            }
        }
    });

    std::string idle_status;
    const unsigned idle_controls = test_libusb_control_requests();
    if (run_control(runtime_dir, instance, idle_status, "status") != 0 ||
        !check_status(idle_status, instance, epoch.value(), "free") ||
        test_libusb_control_requests() != idle_controls) {
        std::fprintf(stderr, "PROOF idle status failed: %s", idle_status.c_str());
        return 1;
    }
    std::fprintf(stderr, "PROOF idle_status=%s", idle_status.c_str());
    const std::string control_output_path = runtime_dir + "/control.ts";
    const std::string missing_runtime_dir = runtime_dir + "/missing-runtime";
    if (run_control_ts_failure(missing_runtime_dir, instance) != 0) {
        std::fprintf(stderr, "PROOF control failure regression failed\n");
        serving.store(false);
        server.join();
        (void)owned.close(Timeout{15000U});
        return 1;
    }
    if (run_control_ts(runtime_dir, instance, control_output_path, device, *family) != 0) {
        std::fprintf(stderr, "PROOF control ts regression failed\n");
        serving.store(false);
        server.join();
        (void)owned.close(Timeout{15000U});
        return 1;
    }
    const unsigned tune_requests_after_control = test_libusb_tune_requests();

    // 2. Real client process over the real AF_UNIX endpoints; bottom USB is
    //    the loopback.  A feeder injects SIGNAL_DETECTED and K BDA packets
    //    once the consumer is attached.
    std::atomic<bool> fed{false};
    std::thread feeder([&]() {
        for (unsigned i = 0U; i < 5000U && !family->stream_attached(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!family->stream_attached()) {
            std::fprintf(stderr, "PROOF attach wait expired tune_requests=%u\n",
                         test_libusb_tune_requests());
            return;
        }
        std::string streaming_status;
        const unsigned controls_before_status = test_libusb_control_requests();
        if (run_control(runtime_dir, instance, streaming_status, "status") != 0 ||
            !check_status(streaming_status, instance, epoch.value(), "streaming") ||
            test_libusb_control_requests() != controls_before_status) {
            std::fprintf(stderr, "PROOF streaming status failed controls=%u/%u: %s",
                         controls_before_status, test_libusb_control_requests(),
                         streaming_status.c_str());
            return;
        }
        std::fprintf(stderr, "PROOF streaming_status=%s", streaming_status.c_str());
        for (std::size_t i = 0U; i < MAX_URBS; ++i) {
            if (!wait_transfer_pending(device.transfers[i])) {
                std::fprintf(stderr, "PROOF transfer not pending index=%zu\n", i);
                return;
            }
            if (test_libusb_deliver_control_to(device.transfers[i],
                                               MSG_SMS_SIGNAL_DETECTED_IND) != 0) {
                std::fprintf(stderr, "PROOF signal delivery failed index=%zu\n", i);
                return;
            }
            if (!wait_transfer_pending(device.transfers[i])) {
                std::fprintf(stderr, "PROOF transfer resubmit missing index=%zu\n", i);
                return;
            }
        }
        fed.store(deliver_ts(device, 0x41U, kPackets));
        if (!fed.load()) std::fprintf(stderr, "PROOF TS delivery failed\n");
    });

    const std::string output_path = runtime_dir + "/capture.ts";
    const int client_status = run_client(runtime_dir, instance, output_path, "count", "5");
    std::fprintf(stderr, "PROOF client_status=%d\n", client_status);
    feeder.join();
    serving.store(false);
    server.join();

    if (!fed.load()) { std::fprintf(stderr, "PROOF feed_failed\n"); return 1; }
    if (client_status != 0) {
        std::fprintf(stderr, "PROOF client_status=%d\n", client_status);
        return 1;
    }
    const auto expected = expected_ts(0x41U, kPackets);
    const auto actual = read_file(output_path);
    if (actual != expected) {
        std::fprintf(stderr, "PROOF byte_mismatch actual=%zu expected=%zu\n",
                     actual.size(), expected.size());
        return 1;
    }
    if (test_libusb_last_tune_hz() != 473142857U ||
        test_libusb_tune_requests() != tune_requests_after_control + 1U) {
        std::fprintf(stderr, "PROOF tune hz=%u requests=%u\n",
                     test_libusb_last_tune_hz(), test_libusb_tune_requests());
        return 1;
    }
    std::fprintf(stderr, "PROOF ts_bytes=%zu hz=%u tune_requests=%u\n",
                 actual.size(), test_libusb_last_tune_hz(), test_libusb_tune_requests());

    const auto closed = owned.close(Timeout{15000U});
    if (!closed || closed.value() !=
            dtv::userland::host_listener_context::CloseState::quiesced) {
        std::fprintf(stderr, "PROOF close quiesced=%d error=%s\n",
                     closed && closed.value() ==
                         dtv::userland::host_listener_context::CloseState::quiesced,
                     error_string(closed ? Error::OK : closed.error()));
        return 1;
    }
    std::fprintf(stderr, "PROOF closed quiesced=true error=OK\n");

    // 3. A short no-TS run ends normally with an empty output file.
    {
        const std::string dir2 = make_private_dir();
        assert(!dir2.empty());
        const std::string inst2 = "proofinst0002";
        auto a2 = assemble_siano_context(config, inst2, dir2.c_str(), epoch.value(),
                                         SIANO_RUNTIME_VERSION);
        if (!a2) {
            std::fprintf(stderr, "PROOF noTs assemble_error=%s\n",
                         error_string(a2.error()));
            return 1;
        }
        dtv::userland::host_listener_context::EnclosureContext& owned2 =
            *a2.value().context;
        std::atomic<bool> serving2{true};
        std::thread server2([&]() {
            while (serving2.load()) {
                const auto polled = owned2.poll_once(Timeout{50U});
                if (!polled) break;
            }
        });
        const std::string out2 = dir2 + "/empty.ts";
        const int status2 = run_client(dir2, inst2, out2, "time", "1");
        serving2.store(false);
        server2.join();
        const auto bytes2 = read_file(out2);
        const auto closed2 = owned2.close(Timeout{15000U});
        if (status2 != 0 || !bytes2.empty() || !closed2 ||
            closed2.value() !=
                dtv::userland::host_listener_context::CloseState::quiesced) {
            std::fprintf(stderr,
                         "PROOF noTs status=%d bytes=%zu quiesced=%d error=%s\n", status2,
                         bytes2.size(), closed2 && closed2.value() ==
                             dtv::userland::host_listener_context::CloseState::quiesced,
                         error_string(closed2 ? Error::OK : closed2.error()));
            return 1;
        }
        std::fprintf(stderr, "PROOF no_ts status=0 bytes=0 quiesced=true error=OK\n");
    }

    // 4. The README form omits --runtime-dir with XDG_RUNTIME_DIR set;
    //    daemon, control clients, and TS clients must use the same Core root.
    {
        const std::string dir3 = make_private_dir();
        const std::string xdg3 = make_private_dir();
        assert(!dir3.empty() && !xdg3.empty());
        const std::string inst3 = "proofinst0003";
        const char* previous_xdg = std::getenv("XDG_RUNTIME_DIR");
        const std::string saved_xdg = previous_xdg != nullptr ? previous_xdg : "";
        const bool had_xdg = previous_xdg != nullptr;
        if (setenv("XDG_RUNTIME_DIR", xdg3.c_str(), 1) != 0) {
            if (had_xdg) (void)setenv("XDG_RUNTIME_DIR", saved_xdg.c_str(), 1);
            else (void)unsetenv("XDG_RUNTIME_DIR");
            std::fprintf(stderr, "PROOF xdgRoot setenv failed\n");
            return 1;
        }
        const auto selected_root = posix::select_publish_runtime_root(
            "siano-userland", inst3.c_str());
        if (!selected_root) {
            if (had_xdg) (void)setenv("XDG_RUNTIME_DIR", saved_xdg.c_str(), 1);
            else (void)unsetenv("XDG_RUNTIME_DIR");
            std::fprintf(stderr, "PROOF xdgRoot select_error=%s\n",
                         error_string(selected_root.error()));
            return 1;
        }
        const std::string root3(selected_root.value());
        if (root3 != xdg3) {
            if (had_xdg) (void)setenv("XDG_RUNTIME_DIR", saved_xdg.c_str(), 1);
            else (void)unsetenv("XDG_RUNTIME_DIR");
            std::fprintf(stderr, "PROOF xdgRoot selected=%s expected=%s\n",
                         root3.c_str(), xdg3.c_str());
            return 1;
        }
        auto a3 = assemble_siano_context(config, inst3, nullptr, epoch.value(),
                                         SIANO_RUNTIME_VERSION);
        if (!a3) {
            if (had_xdg) (void)setenv("XDG_RUNTIME_DIR", saved_xdg.c_str(), 1);
            else (void)unsetenv("XDG_RUNTIME_DIR");
            std::fprintf(stderr, "PROOF xdgRoot assemble_error=%s\n",
                         error_string(a3.error()));
            return 1;
        }
        const auto expected_endpoint = dtv::userland::host_runtime::endpoint_path(
            {root3, "siano-userland", inst3,
             dtv::userland::host_runtime::EndpointAccess::private_user});
        dtv::userland::host_listener_context::EnclosureContext& owned3 =
            *a3.value().context;
        std::atomic<bool> serving3{true};
        std::thread server3([&]() {
            while (serving3.load()) {
                const auto polled = owned3.poll_once(Timeout{50U});
                if (!polled) break;
            }
        });
        std::string status3_json;
        const int ctl_status3 = run_control(root3, inst3, status3_json, "status", false);
        std::string list3_text;
        const int ctl_list3 = run_control(root3, inst3, list3_text, "list", false);
        const std::string out3 = dir3 + "/empty.ts";
        const int status3 = run_client(root3, inst3, out3, "time", "1", false);
        if (had_xdg) (void)setenv("XDG_RUNTIME_DIR", saved_xdg.c_str(), 1);
        else (void)unsetenv("XDG_RUNTIME_DIR");
        serving3.store(false);
        server3.join();
        const auto bytes3 = read_file(out3);
        const auto closed3 = owned3.close(Timeout{15000U});
        if (!expected_endpoint || a3.value().endpoint_path != expected_endpoint.value() ||
            ctl_status3 != 0 || status3_json.find("\"state\":\"ready\"") ==
                std::string::npos || ctl_list3 != 0 ||
            list3_text.find("ready=1 usb-functions=1 receivers=1 readers=0") ==
                std::string::npos ||
            status3 != 0 || !bytes3.empty() || !closed3 ||
            closed3.value() !=
                dtv::userland::host_listener_context::CloseState::quiesced) {
            std::fprintf(stderr,
                         "PROOF xdgRoot status=%d list=%d text=%s ts=%d bytes=%zu quiesced=%d error=%s\n",
                         ctl_status3, ctl_list3, list3_text.c_str(), status3, bytes3.size(), closed3 && closed3.value() ==
                             dtv::userland::host_listener_context::CloseState::quiesced,
                         error_string(closed3 ? Error::OK : closed3.error()));
            return 1;
        }
        std::fprintf(stderr, "PROOF xdg_root root=%s status=0 list=0 ts=0 bytes=0 quiesced=true error=OK\n",
                     root3.c_str());
    }
    {
        const std::string dir4 = make_private_dir();
        assert(!dir4.empty());
        const std::string inst4 = "proofinst0004";
        auto a4 = assemble_siano_context(config, inst4, dir4.c_str(), epoch.value(),
                                         SIANO_RUNTIME_VERSION);
        if (!a4) {
            std::fprintf(stderr, "PROOF output failure assemble_error=%s\n",
                         error_string(a4.error()));
            return 1;
        }
        dtv::userland::host_listener_context::EnclosureContext& owned4 =
            *a4.value().context;
        SianoFamily* family4 = a4.value().family;
        if (family4 == nullptr) {
            std::fprintf(stderr, "PROOF control output failure regression failed\n");
            (void)owned4.close(Timeout{15000U});
            return 1;
        }
        std::atomic<bool> serving4{true};
        std::thread server4([&]() {
            while (serving4.load()) {
                const auto polled = owned4.poll_once(Timeout{50U});
                if (!polled) {
                    std::fprintf(stderr, "PROOF output failure server poll error=%u\n",
                                 static_cast<unsigned>(polled.error()));
                    break;
                }
            }
        });
        const int output_failure_status = run_control_ts_write_failure(
            dir4, inst4, family4->adapter().device(), *family4);
        serving4.store(false);
        server4.join();
        if (output_failure_status != 0) {
            std::fprintf(stderr, "PROOF control output failure regression failed\n");
            (void)owned4.close(Timeout{15000U});
            return 1;
        }
        const auto closed4 = owned4.close(Timeout{15000U});
        if (!closed4 || closed4.value() !=
                dtv::userland::host_listener_context::CloseState::quiesced) {
            std::fprintf(stderr, "PROOF output failure cleanup quiesced=%d error=%s\n",
                         closed4 && closed4.value() ==
                             dtv::userland::host_listener_context::CloseState::quiesced,
                         error_string(closed4 ? Error::OK : closed4.error()));
            return 1;
        }
        std::fprintf(stderr, "PROOF output failure cleanup quiesced=true error=OK\n");
    }
    return 0;
}
