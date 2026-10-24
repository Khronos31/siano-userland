// SPDX-License-Identifier: GPL-2.0-only
#include "host_ctl.h"

#if defined(_WIN32)
#include "px4_windows_args.h"
#endif

#include <cstdlib>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

const dtv::userland::host_ctl::Profile& profile() noexcept
{
    static const dtv::userland::host_ctl::Profile value{
        "sianoctl", "siano-userland", SIANO_RUNTIME_VERSION,
        dtv::userland::identity::Product::siano, false, false, nullptr, nullptr};
    return value;
}

std::string default_runtime_directory() noexcept
{
#ifdef _WIN32
    const char* base = std::getenv("LOCALAPPDATA");
    if (base == nullptr || base[0] == '\0') base = std::getenv("TEMP");
    return base != nullptr ? std::string(base) : std::string();
#else
    const char* base = std::getenv("XDG_RUNTIME_DIR");
    if (base == nullptr || base[0] == '\0') base = std::getenv("TMPDIR");
    if (base == nullptr || base[0] == '\0') base = "/tmp";
    if (base[0] != '/') return {};
    std::string path(base);
    while (path.size() > 1U && path.back() == '/') path.pop_back();
    path += "/dtv-userland-";
    path += std::to_string(static_cast<unsigned long long>(::geteuid()));
    return path;
#endif
}

void select_runtime_directory(std::vector<std::string>& args)
{
    for (std::size_t i = 1U; i < args.size(); ++i) {
        if (args[i] == "--runtime-dir") return;
        if (args[i] == "--help" || args[i] == "-h" || args[i] == "--version")
            return;
    }
    args.insert(args.begin() + 1, "--runtime-dir");
    args.insert(args.begin() + 2, default_runtime_directory());
}

}  // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    std::vector<std::string> owned_argv =
        px4::userland::cli::windows_argv_utf8(argc, argv);
    select_runtime_directory(owned_argv);
    std::vector<const char*> argv_views;
    argv_views.reserve(owned_argv.size());
    for (const std::string& value : owned_argv) argv_views.push_back(value.c_str());
    return dtv::userland::host_ctl::run_control_cli(
        static_cast<int>(argv_views.size()), argv_views.data(), profile());
#else
    std::vector<std::string> owned_argv;
    owned_argv.reserve(static_cast<std::size_t>(argc) + 2U);
    for (int i = 0; i < argc; ++i) owned_argv.emplace_back(argv[i]);
    select_runtime_directory(owned_argv);
    std::vector<const char*> argv_views;
    argv_views.reserve(owned_argv.size());
    for (const std::string& value : owned_argv) argv_views.push_back(value.c_str());
    return dtv::userland::host_ctl::run_control_cli(
        static_cast<int>(argv_views.size()), argv_views.data(), profile());
#endif
}
