/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SIANO_L3_FAMILY_H
#define SIANO_L3_FAMILY_H

#include "siano-enclosure.h"

namespace siano::runtime {

px4::userland::Result<SianoAssembly> assemble_siano_l3_context(
    const SianoRuntimeConfig& config, const std::string& requested_instance,
    const char* runtime_directory, const dtv::userland::wire::Epoch& epoch,
    const char* product_version, AssemblyDiagnostic* diagnostic = nullptr) noexcept;

}  // namespace siano::runtime

#endif
