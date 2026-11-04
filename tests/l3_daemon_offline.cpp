/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "siano-l3-family.h"
#include "siano-l3.h"

#include <dtv/userland/wire.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
siano_l3_adapter* siano_l3_offline_adapter(void);
void siano_l3_offline_diagnostics(void);
}

namespace {

void fill_firmware(std::vector<unsigned char>& image)
{
    image.assign(12U + 85828U, 0U);
    image[0] = 0xadU; image[1] = 0x00U; image[2] = 0x00U; image[3] = 0x00U;
    image[4] = 0x44U; image[5] = 0x4fU; image[6] = 0x01U; image[7] = 0x00U;
    image[8] = 0x60U; image[9] = 0x02U; image[10] = 0x04U; image[11] = 0x00U;
    for (std::size_t i = 12U; i < image.size(); ++i)
        image[i] = static_cast<unsigned char>(i & 0xffU);
}

bool write_file(const std::string& path, const std::vector<unsigned char>& data)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s SIANO_TS RUNTIME_DIR OUTPUT_TS\n", argv[0]);
        return 2;
    }
    const std::string ts_tool = argv[1];
    const std::string runtime = argv[2];
    const std::string output = argv[3];
    const std::string firmware_path = runtime + "/firmware.bin";
    std::vector<unsigned char> firmware;
    fill_firmware(firmware);
    if (!write_file(firmware_path, firmware)) return 2;

    siano::runtime::SianoRuntimeConfig config;
    config.own_usb = true;
    config.selector_text = "usb:5-1.2";
    config.firmware = firmware_path;
    config.access = dtv::userland::host_runtime::EndpointAccess::private_user;
    dtv::userland::wire::Epoch epoch{};
    for (std::size_t i = 0U; i < epoch.size(); ++i)
        epoch[i] = static_cast<std::uint8_t>(0x40U + i);

    auto assembly = siano::runtime::assemble_siano_l3_context(
        config, "offline", runtime.c_str(), epoch, "0.0.0-test", nullptr);
    if (!assembly) {
        std::fprintf(stderr, "assemble: %s\n",
                     dtv::userland::error_string(assembly.error()));
        return 3;
    }
    auto& context = *assembly.value().context;
    std::atomic<bool> stop{false};
    std::thread poller([&]() {
        while (!stop.load())
            (void)context.poll_once(dtv::userland::Timeout{100U});
    });

    const std::string endpoint = assembly.value().endpoint_path;
    for (unsigned i = 0U; i < 100U && access(endpoint.c_str(), F_OK) != 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const pid_t child = fork();
    if (child == 0) {
        const std::string args[] = {
            ts_tool, "--runtime-dir", runtime, "--instance", "offline",
            "-c", "T27", "--system", "ISDB-T", "--receiver", "0",
            "-t", "3", "-o", output};
        std::vector<char*> raw;
        for (const auto& arg : args) raw.push_back(const_cast<char*>(arg.c_str()));
        raw.push_back(nullptr);
        execv(ts_tool.c_str(), raw.data());
        _exit(127);
    }
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) != child) status = -1;
    const int client_exit = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    siano_l3_offline_diagnostics();
    stop.store(true);
    poller.join();
    const auto closed = context.close(dtv::userland::Timeout{5000U});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::uint64_t size = 0U;
    if (FILE* file = std::fopen(output.c_str(), "rb")) {
        std::fseek(file, 0, SEEK_END);
        size = static_cast<std::uint64_t>(std::ftell(file));
        std::fclose(file);
    }
    siano_l3_stats stats{};
    const bool have_stats = siano_l3_offline_adapter() != nullptr &&
        siano_l3_adapter_stats(siano_l3_offline_adapter(), &stats) == DTV_L3_OK;
    std::printf("CLIENT_EXIT=%d\n", client_exit);
    std::printf("CLOSE=%s\n", closed ? "OK" : "FAILED");
    std::printf("TS_SIZE=%llu\n", static_cast<unsigned long long>(size));
    if (have_stats) {
        std::printf("OWNER_SUB=%llu\n", static_cast<unsigned long long>(stats.owner_in_submits));
        std::printf("OWNER_COMP=%llu\n", static_cast<unsigned long long>(stats.owner_in_completions));
        std::printf("TS_PACKETS=%llu\n", static_cast<unsigned long long>(stats.ts_packets));
        std::printf("TS_TEI=%llu\n", static_cast<unsigned long long>(stats.ts_tei_errors));
        std::printf("TS_CC=%llu\n", static_cast<unsigned long long>(stats.ts_continuity_errors));
    }
    return client_exit == 0 && size > 0U && closed ? 0 : 1;
}
