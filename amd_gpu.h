#pragma once
#include "../metric_common.h"
#include <fstream>
#include <string>
#include <cstdint>

namespace MystiqueMetrics::AmdGpu {
inline std::string findTemperatureSensor(const std::string& root="/sys/class/hwmon") {
    for(int i=0;i<15;i++) {
        const auto base = root+"/hwmon"+std::to_string(i);
        if(Common::firstLine(base+"/name")=="amdgpu") return base+"/temp1_input";
    }
    return {};
}
inline std::string findLoadSensor(const std::string& root="/sys/class/drm") {
    return Common::firstBusyPath(5, root);
}
// The following GPU temperature / busy / clock algorithm is extracted from
// the original AMD branch; the original VRAM routine is deliberately retained
// in main_gui.cpp and called unchanged to avoid altering known-good behavior.
template <typename Data>
inline void update(Data& currentData, const std::string& globalGpuTempPath,
                   const std::string& globalGpuLoadPath) {
    if(!globalGpuTempPath.empty()) {
        float v = Common::readFloatFile(globalGpuTempPath);
        currentData.gpuTemp = v / 1000.0f;
        if(currentData.gpuTemp < 1.0f)
            currentData.gpuTemp = v;
    }
    if(!globalGpuLoadPath.empty())
        currentData.gpuUsage = Common::readFloatFile(globalGpuLoadPath);

    std::string activeCardDir = "/sys/class/drm/card0/device";
    if(!globalGpuLoadPath.empty()) {
        const size_t slash = globalGpuLoadPath.rfind('/');
        if(slash != std::string::npos)
            activeCardDir = globalGpuLoadPath.substr(0, slash);
    }

    currentData.gpuFrequency = 0;
    std::ifstream s(activeCardDir + "/pp_dpm_sclk");
    std::string line;
    while(std::getline(s, line)) {
        if(line.find('*') == std::string::npos)
            continue;
        auto p = line.find("MHz");
        if(p == std::string::npos) p = line.find("Mhz");
        if(p == std::string::npos) p = line.find("mhz");
        if(p != std::string::npos) {
            auto q = line.rfind(' ', p);
            if(q != std::string::npos) {
                try {
                    currentData.gpuFrequency = static_cast<uint16_t>(
                        std::stoi(line.substr(q + 1, p - q - 1))
                    );
                } catch(...) {
                    currentData.gpuFrequency = 0;
                }
            }
        }
        break;
    }
    // Keep original Intel clock fallback for odd AMD sysfs layouts.
    if(currentData.gpuFrequency == 0) {
        std::ifstream intelFreq(activeCardDir + "/gt_cur_freq_mhz");
        int mhz = 0;
        if(intelFreq >> mhz && mhz > 0)
            currentData.gpuFrequency = static_cast<uint16_t>(mhz);
    }
}
} // namespace MystiqueMetrics::AmdGpu
