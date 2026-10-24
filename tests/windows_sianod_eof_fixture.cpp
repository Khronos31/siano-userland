/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sianod-daemon-lifecycle.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    const bool monitor_stdin = argc == 2 &&
        std::strcmp(argv[1], "--exit-on-stdin-eof") == 0;
    if (argc > 2 || (argc == 2 && !monitor_stdin)) return 2;

    std::fprintf(stderr, "sianod fixture ready\n");
    std::fflush(stderr);
    return px4::userland::run_sianod_daemon_lifecycle(
        monitor_stdin,
        []() noexcept { ::Sleep(10U); },
        []() noexcept {
            std::fprintf(stderr, "sianod fixture cleanup complete\n");
            std::fflush(stderr);
            return 0;
        },
        []() noexcept {});
}
