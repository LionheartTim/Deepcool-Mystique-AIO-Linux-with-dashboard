#pragma once
#include "../metric_common.h"
#include <cstdio>
#include <cstdint>

namespace MystiqueMetrics::NvidiaGpu {
inline bool available() {
    FILE* nv=popen("which nvidia-smi >/dev/null 2>&1 && nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
    if (!nv) return false;
    int v=0;
    const bool ok=(fscanf(nv,"%d",&v)==1);
    pclose(nv);
    return ok;
}
inline float readVramUsagePercent() {
    FILE* pipe = popen(
        "nvidia-smi --query-gpu=memory.used,memory.total "
        "--format=csv,noheader,nounits 2>/dev/null", "r");
    if (!pipe) return -1.0f;
    unsigned long long usedMiB = 0, totalMiB = 0;
    const int result = fscanf(pipe, "%llu, %llu", &usedMiB, &totalMiB);
    pclose(pipe);
    if (result == 2 && totalMiB > 0)
        return static_cast<float>(100.0 * usedMiB / totalMiB);
    return -1.0f;
}
template <typename Data>
inline void update(Data& data) {
    data.vramUsage = readVramUsagePercent();
    FILE* p=popen("nvidia-smi --query-gpu=temperature.gpu,utilization.gpu,clocks.current.graphics --format=csv,noheader,nounits 2>/dev/null","r");
    if(p) {
        int a,b,c;
        if(fscanf(p,"%d, %d, %d",&a,&b,&c)==3) {
            data.gpuTemp=a; data.gpuUsage=b; data.gpuFrequency=static_cast<uint16_t>(c);
        }
        pclose(p);
    }
}
} // namespace MystiqueMetrics::NvidiaGpu
