/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SIANO_ENCLOSURE_H
#define SIANO_ENCLOSURE_H

#include "siano-runtime-backend.h"

#include <dtv/userland/identity.h>
#include <dtv/userland/records.h>

#include <cstdint>
#include <memory>
#include <string>

namespace siano::runtime {

// Read-only USB facts of the selected RIO function (no claim, no open).
struct SianoFacts final {
    std::uint16_t vendor_id = 0U;
    std::uint16_t product_id = 0U;
    std::uint8_t bus = 0U;
    std::uint8_t address = 0U;
    std::uint8_t ports[8]{};
    std::size_t port_count = 0U;
    bool has_bus = false;
    bool has_address = false;
};

// resolve_rio_facts never claims: it reports the VID/PID and bus/port topology
// of the selected function so startup can build the public identity before the
// native claim.
px4::userland::Result<SianoFacts> resolve_rio_facts(
    libusb_context* usb, const siano_device_selector& selector,
    const std::string& selector_text) noexcept;

// Canonical auto identity of one RIO function from its observed topology.
px4::userland::Result<dtv::userland::identity::ResolvedIdentity> siano_identity(
    const SianoFacts& facts) noexcept;

// Builds the immutable LIST for the single-function, single-receiver RIO
// enclosure: product "siano-userland", topology complete, one function, one
// terrestrial receiver 0 and an empty reader set (no external CARD).
dtv::userland::Result<void> build_siano_control_context(
    const SianoFacts& facts, const std::string& instance,
    const dtv::userland::wire::Epoch& epoch, const char* product_version,
    const px4::userland::TunerService& tuner,
    px4::userland::ipc::posix::ControlContext& out) noexcept;

struct SianoAssembly final {
    std::unique_ptr<px4::userland::ipc::posix::EnclosureContext> context;
    // Borrowed pointer to the family owned by context; valid until the context
    // has quiesced.  Proofs use it to reach the accepted S device.
    SianoFamily* family = nullptr;
    std::string instance;
    std::string lease_identity;
};

// Builds the family, resolves the identity, claims/initializes the native
// device, assembles the P context, reserves the namespace and publishes the
// endpoints.  The same function is used by the real daemon and the proof.
px4::userland::Result<SianoAssembly> assemble_siano_context(
    const SianoRuntimeConfig& config, const std::string& requested_instance,
    const char* runtime_directory, const dtv::userland::wire::Epoch& epoch,
    const char* product_version) noexcept;

}  // namespace siano::runtime

#endif  // SIANO_ENCLOSURE_H
