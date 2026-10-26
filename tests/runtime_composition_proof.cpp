/* SPDX-License-Identifier: GPL-2.0-or-later */
// Link-closure probe for the S product.  It references every P runtime symbol
// the Siano daemon and client need, so a missing subset symbol fails the link
// here instead of at the product link.  It is never executed with the
// reference branch.
#include <dtv/userland/host_listener_context/enclosure_context.h>
#include "px4/tuner_service.h"
#include <dtv/userland/host_facade/px4d_signals.h>
#include "siano-receiver-adapter.h"

int main(int argc, char**) noexcept
{
    if (argc == 12345) {
        using dtv::userland::host_listener_context::EnclosureContext;
        using ReserveNamespaceFunction = dtv::userland::Result<void>
            (EnclosureContext::*)() noexcept;
        (void)&EnclosureContext::assemble;
        (void)static_cast<ReserveNamespaceFunction>(
            &EnclosureContext::reserve_namespace);
        (void)&EnclosureContext::publish;
        (void)&EnclosureContext::close;
        (void)&px4::userland::TunerService::acquire;
        (void)&px4::userland::px4d::install_signal_handlers;
        (void)&siano::capture::ReceiverAdapter::open;
        (void)&siano::capture::ReceiverAdapter::program_hz;
        (void)&siano::capture::Consumer::start;
    }
    return 0;
}
