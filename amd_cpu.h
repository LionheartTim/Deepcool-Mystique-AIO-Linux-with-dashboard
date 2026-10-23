#pragma once
#include "../metric_common.h"

namespace MystiqueMetrics::AmdCpu {
// Same priority and bounds as the original, working scanSensors() routine.
inline std::string findTemperatureSensor(const std::string& root = "/sys/class/hwmon") {
    for (int i=0;i<15;i++) {
        const auto base = root+"/hwmon"+std::to_string(i);
        const auto drv = Common::firstLine(base+"/name");
        if (drv=="coretemp" || drv=="k10temp" || drv=="zenpower")
            return base+"/temp1_input";
    }
    return "/sys/class/thermal/thermal_zone0/temp";
}
inline void updateTemperature(float& cpuTemp, const std::string& path) {
    if(!path.empty()) cpuTemp=Common::readFloatFile(path)/1000.0f;
    if(cpuTemp<1) cpuTemp=Common::readFloatFile(path);
}
} // namespace MystiqueMetrics::AmdCpu
