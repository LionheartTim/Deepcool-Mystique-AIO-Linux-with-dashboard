#pragma once
#include "../metric_common.h"
#include <fstream>
#include <string>
#include <cstdint>

namespace MystiqueMetrics::IntelGpu {
inline std::string findTemperatureSensor(const std::string& root="/sys/class/hwmon") {
    for(int i=0;i<64;i++) {
        const auto base=root+"/hwmon"+std::to_string(i);
        const auto name=Common::firstLine(base+"/name");
        if(name=="i915" || name=="xe") return base+"/temp1_input";
    }
    return {};
}
inline std::string findLoadSensor(const std::string& root="/sys/class/drm") {
    return Common::firstBusyPath(32, root);
}
template <typename Data>
inline void update(Data& data, const std::string& temperaturePath,
                   const std::string& loadPath) {
    // No universal Intel GPU VRAM utilization API; do not invent values.
    data.vramUsage=-1.0f;
    if (!temperaturePath.empty()) {
        const float temp=Common::readFloatFile(temperaturePath);
        data.gpuTemp = temp/1000.0f;
        if (data.gpuTemp < 1.0f) data.gpuTemp=temp;
    }
    if (!loadPath.empty()) data.gpuUsage=Common::readFloatFile(loadPath);
    std::string activeCardDir="/sys/class/drm/card0/device";
    if(!loadPath.empty()) {
        const size_t slash=loadPath.rfind('/');
        if(slash!=std::string::npos) activeCardDir=loadPath.substr(0,slash);
    }
    data.gpuFrequency=0;
    std::ifstream intelFreq(activeCardDir+"/gt_cur_freq_mhz");
    int mhz=0;
    if(intelFreq >> mhz && mhz>0)
        data.gpuFrequency=static_cast<uint16_t>(mhz);
}
} // namespace MystiqueMetrics::IntelGpu
