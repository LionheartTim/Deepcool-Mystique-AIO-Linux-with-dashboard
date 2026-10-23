#pragma once
// MYSTIQUE STEP 6E THERMAL ALERTS v1
// Read-only temperature validation. Never sends USB/SD commands or shuts down the OS.
// A temperature is not evidence of actual thermal throttling.
#include <cmath>
#include <fstream>
#include <string>

namespace MystiqueThermal {
struct Reading {
    bool valid = false;
    double celsius = 0.0;
};

inline Reading readSensor(const std::string& path) {
    if (path.empty()) return {};
    std::ifstream input(path);
    double raw = 0.0;
    if (!(input >> raw) || !std::isfinite(raw)) return {};
    // hwmon reports milli-Celsius, other temperature fallbacks may report Celsius.
    const double c = raw > 1000.0 ? raw / 1000.0 : raw;
    if (c < -20.0 || c > 130.0) return {};
    return {true, c};
}

enum class Level { Normal = 0, Warning = 1, Critical = 2, Emergency = 3 };
struct Transition {
    Level level = Level::Normal;
    double celsius = 0.0;
    bool newAlert = false;
    bool recovered = false;
};

struct Monitor {
    int hotSamples = 0;
    int coolSamples = 0;
    Level active = Level::Normal;

    // Called only once per second, after the existing Metrics update.
    Transition tick(Reading r, bool cpu) {
        if (!r.valid) return {}; // Never trigger shutdown/alarm from missing data.
        const double c = r.celsius;
        hotSamples = c >= 90.0 ? (hotSamples < 10 ? hotSamples + 1 : 10) : 0;
        coolSamples = c <= 87.0 ? (coolSamples < 30 ? coolSamples + 1 : 30) : 0;

        Level next = Level::Normal;
        if (c >= 100.0) next = Level::Emergency;
        else if (cpu && c >= 95.0) next = Level::Critical;
        else if (hotSamples >= 10) next = Level::Warning;
        if (static_cast<int>(next) > static_cast<int>(active)) {
            active = next;
            return {active, c, true, false};
        }
        if (active != Level::Normal && coolSamples >= 30) {
            active = Level::Normal;
            hotSamples = 0;
            return {active, c, false, true};
        }
        return {};
    }

    bool alarming() const { return active != Level::Normal; }
};
} // namespace MystiqueThermal
