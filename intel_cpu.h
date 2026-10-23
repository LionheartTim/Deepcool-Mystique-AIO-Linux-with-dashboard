#pragma once
#include "../metric_common.h"
#include <string>
#include <fstream>

namespace MystiqueMetrics::IntelCpu {
// Prefer CPU Package over an individual core, then fall back safely.
inline std::string findTemperatureSensor(const std::string& root = "/sys/class/hwmon") {
    std::string fallback;
    for (int i=0; i<64; ++i) {
        const std::string base = root+"/hwmon"+std::to_string(i);
        if (Common::firstLine(base+"/name") != "coretemp") continue;
        for (int j=1; j<=32; ++j) {
            const std::string p = base+"/temp"+std::to_string(j);
            std::ifstream tempFile(p+"_input");
            if (!tempFile.good()) continue;
            if (fallback.empty()) fallback = p+"_input";
            if (Common::firstLine(p+"_label").find("Package id") != std::string::npos)
                return p+"_input";
        }
    }
    if (!fallback.empty()) return fallback;
    return "/sys/class/thermal/thermal_zone0/temp";
}
inline void updateTemperature(float& cpuTemp, const std::string& path) {
    if (path.empty()) return;
    float v = Common::readFloatFile(path);
    cpuTemp = v / 1000.0f;
    if (cpuTemp < 1.0f) cpuTemp = v;
}
} // namespace MystiqueMetrics::IntelCpu
