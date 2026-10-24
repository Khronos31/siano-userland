/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SIANOD_DAEMON_LIFECYCLE_H
#define SIANOD_DAEMON_LIFECYCLE_H

#ifdef _WIN32
#include <dtv/userland/host_facade/windows_stdin.h>
#endif
#include "px4d_signals.h"

namespace px4::userland {

template <typename PollOnce, typename CloseContext, typename AfterClose>
int run_sianod_daemon_lifecycle(bool exit_on_stdin_eof,
                                PollOnce&& poll_once,
                                CloseContext&& close_context,
                                AfterClose&& after_close) noexcept
{
#ifdef _WIN32
    cli::StdinEofMonitor eof_monitor;
    bool monitor_start_failed = false;
    if (exit_on_stdin_eof &&
        !eof_monitor.start(::GetStdHandle(STD_INPUT_HANDLE),
            [](void*) noexcept { px4d::request_stop(); }, nullptr)) {
        monitor_start_failed = true;
        px4d::request_stop();
    }
#else
    (void)exit_on_stdin_eof;
#endif

    while (!px4d::stop_requested()) poll_once();
    const int close_status = close_context();

#ifdef _WIN32
    eof_monitor.stop();
#endif
    after_close();
#ifdef _WIN32
    if (monitor_start_failed && close_status == 0) {
        px4d::notify_cleanup_complete();
        return 70;
    }
#endif
    px4d::notify_cleanup_complete();
    return close_status;
}

}  // namespace px4::userland

#endif  // SIANOD_DAEMON_LIFECYCLE_H
