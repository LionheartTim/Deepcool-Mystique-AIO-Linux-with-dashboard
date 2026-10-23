#pragma once
// Pure vendor selection: does not change display mode, GIF, USB or QSettings.
#include <string>
#include "cpu/amd_cpu.h"
#include "cpu/intel_cpu.h"
#include "gpu/amd_gpu.h"
#include "gpu/nvidia_gpu.h"
#include "gpu/intel_gpu.h"

namespace MystiqueMetrics {
enum class CpuRoute { Amd, Intel, Legacy };
enum class GpuRoute { Amd, Nvidia, Intel, Legacy };
inline CpuRoute routeCpu(const std::string& vendor) {
    if (vendor=="amd") return CpuRoute::Amd;
    if (vendor=="intel") return CpuRoute::Intel;
    return CpuRoute::Legacy;
}
inline GpuRoute routeGpu(const std::string& vendor) {
    if (vendor=="amd") return GpuRoute::Amd;
    if (vendor=="nvidia") return GpuRoute::Nvidia;
    if (vendor=="intel") return GpuRoute::Intel;
    return GpuRoute::Legacy; // mixed / unknown: original reader, no guessing
}
} // namespace MystiqueMetrics
