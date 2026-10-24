/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <dtv/userland/discovery_json.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

inline bool make_siano_discovery_enclosure(
    dtv::userland::wire::DiscoveryEnclosure& enclosure,
    std::uint16_t id, const char* model)
{
    const std::size_t size = std::strlen(model);
    if (size == 0U || size > enclosure.model.value.bytes.size()) return false;
    enclosure.enclosure = id;
    enclosure.status = dtv::userland::wire::DiscoveryEnclosureStatus::ready;
    enclosure.model.present = true;
    enclosure.model.value.length = static_cast<std::uint16_t>(size);
    std::memcpy(enclosure.model.value.bytes.data(), model, size);
    enclosure.lnb_15v_supported.present = true;
    enclosure.lnb_15v_supported.value = false;
    enclosure.readers_known = true;
    enclosure.device_count = 1U;
    return true;
}
