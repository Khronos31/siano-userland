/* SPDX-License-Identifier: GPL-2.0-or-later */
// Link-closure probe for the S product.  It references every P runtime symbol
// the Siano daemon and client need, so a missing subset symbol fails the link
// here instead of at the product link.  It is never executed with the
// reference branch.
#include "px4/control_server.h"
#include "px4/enclosure_context.h"
#include "px4/posix_ipc.h"
#include "px4/tuner_service.h"
#include "px4_ts_core.h"
#include "px4d_signals.h"
#include "siano-receiver-adapter.h"

int main(int argc, char**) noexcept
{
    if (argc == 12345) {
        using namespace px4::userland;
        using namespace px4::userland::ipc::posix;
        using namespace px4::userland::cli;
        (void)&EnclosureContext::assemble;
        (void)&PosixControlServer::create;
        (void)&SerialEndpointLease::acquire;
        (void)&TunerService::acquire;
        (void)&TunerService::configure_stream_contract;
        (void)&Px4TsRunner::run_wire;
        (void)&Px4TsRunner::run;
        (void)&px4::userland::px4d::stop_requested;
        (void)&px4::userland::px4d::install_signal_handlers;
        (void)&siano::capture::ReceiverAdapter::open;
        (void)&siano::capture::ReceiverAdapter::program_hz;
        (void)&siano::capture::Consumer::start;
    }
    return 0;
}
