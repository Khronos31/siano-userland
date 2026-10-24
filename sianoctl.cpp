// SPDX-License-Identifier: GPL-2.0-only
#include "host_ctl.h"

#if defined(_WIN32)
#include "px4_windows_args.h"
#endif

#include <string>
#include <vector>

namespace {

const dtv::userland::host_ctl::Profile& profile() noexcept
{
    static const dtv::userland::host_ctl::Profile value{
        "sianoctl", "siano-userland", SIANO_RUNTIME_VERSION,
        dtv::userland::identity::Product::siano, false, false, nullptr, nullptr};
    return value;
}

}  // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    std::vector<std::string> owned_argv =
        px4::userland::cli::windows_argv_utf8(argc, argv);
    std::vector<const char*> argv_views;
    argv_views.reserve(owned_argv.size());
    for (const std::string& value : owned_argv) argv_views.push_back(value.c_str());
    return dtv::userland::host_ctl::run_control_cli(
        static_cast<int>(argv_views.size()), argv_views.data(), profile());
#else
    std::vector<std::string> owned_argv;
    owned_argv.reserve(static_cast<std::size_t>(argc) + 2U);
    for (int i = 0; i < argc; ++i) owned_argv.emplace_back(argv[i]);
    std::vector<const char*> argv_views;
    argv_views.reserve(owned_argv.size());
    for (const std::string& value : owned_argv) argv_views.push_back(value.c_str());
    return dtv::userland::host_ctl::run_control_cli(
        static_cast<int>(argv_views.size()), argv_views.data(), profile());
#endif
}
