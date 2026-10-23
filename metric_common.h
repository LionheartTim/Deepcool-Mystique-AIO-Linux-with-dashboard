#pragma once
// Shared, vendor-neutral Linux file helpers. No USB or GUI operations.
#include <fstream>
#include <string>
#include <cstdlib>

namespace MystiqueMetrics::Common {
inline std::string firstLine(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    if (file) std::getline(file, line);
    // hwmon/name should not have trailing CR on Linux.
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}
inline float readFloatFile(const std::string& path) {
    std::ifstream f(path); float v=0; if(f) f>>v; return v;
}
inline std::string hwmonTemp(const std::string& name, int count = 15,
                            const std::string& root = "/sys/class/hwmon") {
    for (int i=0; i<count; ++i) {
        const std::string base = root + "/hwmon" + std::to_string(i);
        if (firstLine(base+"/name") == name)
            return base+"/temp1_input";
    }
    return {};
}
inline std::string firstBusyPath(int count = 5,
                                const std::string& root = "/sys/class/drm") {
    for (int i=0; i<count; ++i) {
        const std::string path = root+"/card"+std::to_string(i)+"/device/gpu_busy_percent";
        std::ifstream f(path);
        if (f.good()) return path;
    }
    return {};
}
} // namespace MystiqueMetrics::Common
