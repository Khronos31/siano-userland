/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "siano-enclosure.h"
#include "px4/posix_tuner_nonce.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

extern "C" unsigned test_libusb_pid_requests(void);

int main()
{
    using namespace siano::runtime;
    char directory[] = "/tmp/sfeatures-proof-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    SianoRuntimeConfig config;
    config.selector.kind = SIANO_DEVICE_SELECTOR_PORT;
    config.selector.bus = 1U;
    config.selector.ports[0] = 2U;
    config.selector.ports[1] = 3U;
    config.selector.port_count = 2U;
    config.selector_text = "usb:1-2.3";
    config.pids[0] = 0x30U;
    config.pids[1] = 0x2000U;
    config.pid_count = 2U;
    px4::userland::ipc::posix::PosixTunerNonceSource nonce;
    const auto epoch = nonce.generate();
    if (!epoch) return 1;
    auto owned = assemble_siano_context(config, "features", directory,
                                        epoch.value(), "0.0.0-dev");
    if (!owned) return 1;
    auto& assembly = owned.value();
    const unsigned before = test_libusb_pid_requests();
    auto tuned = assembly.family->adapter().program_hz(
        0U, dtv::userland::detail::ReceiverSystem::terrestrial, 545142857U, 2000U);
    if (!tuned || test_libusb_pid_requests() != before + 2U) return 1;
    tuned = assembly.family->adapter().program_hz(
        0U, dtv::userland::detail::ReceiverSystem::terrestrial, 551142857U, 2000U);
    if (!tuned || test_libusb_pid_requests() != before + 2U) return 1;

    // Android wrapped handles may report bus/address but no port path.  An
    // explicit instance must allow this without fabricating a USB topology.
    SianoFacts facts{};
    facts.vendor_id = 0x3275U;
    facts.product_id = 0x0080U;
    facts.has_bus = facts.has_address = true;
    facts.bus = facts.address = 1U;
    px4::userland::ipc::posix::ControlContext control{};
    if (!build_siano_control_context(facts, "features", epoch.value(),
            "0.0.0-dev", assembly.context->tuner_service(), control)) return 1;
    if (control.list.functions.items[0].location.usb_path.present) return 1;
    const auto closed = assembly.context->close(std::chrono::milliseconds(15000));
    if (!closed.quiesced || closed.error != dtv::userland::Error::OK) return 1;
    // Context close removes sockets; remove the remaining private directories.
    const std::string leaf = std::string(directory) + "/siano-userland/v1/features";
    rmdir(leaf.c_str());
    rmdir((std::string(directory) + "/siano-userland/v1").c_str());
    rmdir((std::string(directory) + "/siano-userland").c_str());
    rmdir(directory);
    std::fprintf(stderr, "FEATURES pid_requests=2 (retune unchanged), no-port LIST valid, close=OK\n");
    return 0;
}
