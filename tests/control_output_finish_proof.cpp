/* SPDX-License-Identifier: GPL-2.0-or-later */
#define main siano_ts_program_main
#include "../siano-wire-ts.cpp"
#undef main

#include <string>
#include <unistd.h>

int main()
{
    int error_pipe[2];
    if (pipe(error_pipe) != 0) return 1;
    const int saved_stderr = dup(STDERR_FILENO);
    if (saved_stderr < 0 || dup2(error_pipe[1], STDERR_FILENO) < 0) return 1;
    close(error_pipe[1]);

    const std::vector<ControlRunSummary> summaries{{473142857U, 2U, true}};
    report_control_summaries(summaries, false);
    std::fflush(stderr);
    if (dup2(saved_stderr, STDERR_FILENO) < 0) return 1;
    close(saved_stderr);

    std::string errors;
    std::array<char, 256U> buffer{};
    ssize_t count = 0;
    while ((count = read(error_pipe[0], buffer.data(), buffer.size())) > 0)
        errors.append(buffer.data(), static_cast<std::size_t>(count));
    close(error_pipe[0]);

    const bool unknown_output =
        errors.find("packets=2 bytes=unknown status=output-error") !=
        std::string::npos;
    const bool exit_status = control_exit_status(0, false) == 70 &&
                             control_exit_status(5, false) == 5 &&
                             control_exit_status(0, true) == 0;
    if (!unknown_output || !exit_status) {
        std::fprintf(stderr, "control output finish proof failed: %s", errors.c_str());
        return 1;
    }
    std::fprintf(stderr,
        "control output finish proof: packets=2 bytes=unknown status=output-error exit=70: PASS\n");
    return 0;
}
