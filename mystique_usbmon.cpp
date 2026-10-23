// MYSTIQUE USBMON BINARY V1 - read-only Linux proof-of-transport.
// Standalone tester: does NOT modify motherboard/cooler settings.
// stdin: START, PAUSE, QUIT; stdout: STATE / EVENT / FRAME lines.
// Uses only libc, C++17 and the Linux usbmon binary ABI.
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <poll.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// Kernel Documentation/usb/usbmon.rst: 64-byte record and MON_IOCX_GETX.
struct MonPacket {
    std::uint64_t id;
    std::uint8_t type;
    std::uint8_t xfer_type;
    std::uint8_t epnum;
    std::uint8_t devnum;
    std::uint16_t busnum;
    std::uint8_t flag_setup;
    std::uint8_t flag_data;
    std::int64_t ts_sec;
    std::int32_t ts_usec;
    std::int32_t status;
    std::uint32_t length;
    std::uint32_t len_cap;
    std::uint8_t setup[8];
    std::int32_t interval;
    std::int32_t start_frame;
    std::uint32_t xfer_flags;
    std::uint32_t ndesc;
};
struct MonGetArg {
    MonPacket* hdr;
    void* data;
    std::size_t alloc;
};
static_assert(sizeof(MonPacket) == 64, "Unsupported usbmon packet ABI");
static_assert(offsetof(MonPacket, len_cap) == 36, "Unsupported usbmon field ABI");
#ifndef MON_IOCX_GETX
#define MON_IOCX_GETX _IOW(0x92, 10, MonGetArg)
#endif

using TargetMap = std::map<std::pair<int, int>, std::string>;
struct Monitor { std::string path; int fd = -1; };

static std::string readOne(const fs::path& path) {
    std::ifstream in(path);
    std::string value;
    if (in >> value) return value;
    return {};
}
static TargetMap discover() {
    TargetMap result;
    std::error_code ec;
    const fs::path root("/sys/bus/usb/devices");
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        const std::string vid = readOne(it->path() / "idVendor");
        const std::string pid = readOne(it->path() / "idProduct");
        const char* kind = nullptr;
        if (vid == "0b05" && pid == "19af") kind = "ASUS";
        if (vid == "3633" && pid == "0009") kind = "DEEP";
        if (!kind) continue;
        try {
            const int bus = std::stoi(readOne(it->path() / "busnum"));
            const int dev = std::stoi(readOne(it->path() / "devnum"));
            if (bus > 0 && dev > 0) result[{bus, dev}] = kind;
        } catch (...) { /* Hotplug or incomplete sysfs node. */ }
    }
    return result;
}
static std::string hex(const std::uint8_t* data, std::size_t n) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::uppercase;
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out << ' ';
        out << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return out.str();
}
static std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16]{};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

static std::string formatEvent(const MonPacket& h, const std::uint8_t* p,
                               std::size_t n, const TargetMap& targets) {
    const auto it = targets.find({h.busnum, h.devnum});
    if (it == targets.end()) return {};
    const bool in = (h.epnum & 0x80) != 0;
    if (!((h.type == 'S' && !in) || (h.type == 'C' && in))) return {};
    const std::string& name = it->second;
    const bool ec40 = name == "ASUS" && n >= 2 && p[0] == 0xEC && p[1] == 0x40;
    const bool hidc = name == "DEEP" && n >= 3 &&
        ((p[0] == 0xAA && p[1] == 0x2E) || (p[0] == 0x55 && p[1] == 0x2E));
    std::ostringstream out;
    out << "EVENT " << timestamp() << ' ' << name << ' '
        << (in ? "IN " : "OUT") << " EP" << unsigned(h.epnum & 0x7F) << ' ';
    if (ec40) out << "EC40 ";
    else if (hidc) out << "HIDC-" << hex(p + 2, 1) << ' ';
    else out << "USB ";
    out << (n ? hex(p, std::min<std::size_t>(n, 16)) : "(no payload)");
    if (ec40 && n < 65) out << " [TRUNCATED: " << n << " bytes]";
    // FRAME lines carry a complete 65-byte USB report for the existing decoder.
    if (ec40 && n >= 65)
        out << "\nFRAME " << timestamp() << " ASUS EC40 " << hex(p, 65);
    return out.str();
}
// Windows EC40 stream: ASUS EP0 Control OUT HID SET_REPORT (65 bytes).
// Keep it separate from DeepCool's bulk endpoint 2.
static bool exactAsusEc40Setup(const MonPacket& h) {
    static constexpr std::uint8_t expected[8] =
        {0x21, 0x09, 0xEC, 0x02, 0x02, 0x00, 0x41, 0x00};
    return h.type == 'S' && h.xfer_type == 2 && h.epnum == 0 &&
           std::memcmp(h.setup, expected, sizeof(expected)) == 0;
}
static void printTargets(const TargetMap& targets) {
    bool asusFound = false;
    for (const auto& [address, name] : targets) {
        if (name == "ASUS") asusFound = true;
        std::cout << "STATE TARGET " << name << " bus=" << address.first
                  << " dev=" << address.second
                  << (name == "ASUS" ? " VID:PID=0B05:19AF" : " VID:PID=3633:0009")
                  << '\n';
    }
    if (!asusFound)
        std::cout << "STATE ASUS_NOT_FOUND - check USB detection, not RGB decoding\n";
    std::cout << std::flush;
}
static void closeAll(std::vector<Monitor>& monitors) {
    for (auto& m : monitors) if (m.fd >= 0) ::close(m.fd);
    monitors.clear();
}
static std::vector<std::string> monitorPaths(const TargetMap& targets) {
    std::vector<std::string> paths;
    if (fs::exists("/dev/usbmon0")) {
        paths.push_back("/dev/usbmon0");
    } else {
        std::set<int> buses;
        for (const auto& [address, name] : targets) {
            (void)name;
            buses.insert(address.first);
        }
        for (int bus : buses) paths.push_back("/dev/usbmon" + std::to_string(bus));
    }
    return paths;
}
static bool startMonitors(std::vector<Monitor>& monitors, const TargetMap& targets) {
    closeAll(monitors);
    if (targets.empty()) {
        std::cout << "STATE NO_TARGET_DEVICE\n" << std::flush;
        return false;
    }
    for (const auto& path : monitorPaths(targets)) {
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) monitors.push_back({path, fd});
        else std::cout << "STATE OPEN_FAILED " << path << " "
                       << std::strerror(errno) << '\n';
    }
    if (monitors.empty()) {
        std::cout << "STATE NO_ACCESS - no usbmon device opened (no sudo fallback)\n" << std::flush;
        return false;
    }
    std::cout << "STATE ACTIVE";
    for (const auto& m : monitors) std::cout << ' ' << m.path;
    std::cout << '\n' << std::flush;
    return true;
}
// STEP 7C-DIAG: count captured traffic without recording other devices' data.
struct TrafficStats {
    std::uint64_t asus = 0;
    std::uint64_t asusEc40Full = 0;
    std::uint64_t asusEc40Short = 0;
    std::uint64_t asusRaw = 0;           // All ASUS URB events, including IN requests.
    std::uint64_t asusCtrlOut = 0;       // Submitted EP0 Control OUT.
    std::uint64_t asusCtrlIn = 0;        // Completed EP0 Control IN.
    std::uint64_t asusSetReport = 0;     // Exact Windows HID SET_REPORT setup.
    std::uint64_t asusSetReportEc40 = 0; // Exact setup plus complete EC40.
    std::uint64_t asusFirstSamples = 0;
    std::uint64_t asusFirstOutSamples = 0;
    std::uint64_t deep = 0;
    std::uint64_t readErrors = 0;
};
static void countTraffic(TrafficStats& stats, const MonPacket& p,
                         const std::uint8_t* data, std::size_t n,
                         const TargetMap& targets) {
    const auto target = targets.find({p.busnum, p.devnum});
    if (target == targets.end()) return;
    const bool in = (p.epnum & 0x80) != 0;
    if (target->second == "ASUS") {
        ++stats.asusRaw;
        if (p.xfer_type == 2 && (p.epnum & 0x7F) == 0) {
            if (p.type == 'S' && !in) ++stats.asusCtrlOut;
            if (p.type == 'C' && in) ++stats.asusCtrlIn;
        }
        if (exactAsusEc40Setup(p)) {
            ++stats.asusSetReport;
            if (n >= 65 && data[0] == 0xEC && data[1] == 0x40)
                ++stats.asusSetReportEc40;
        }
    }
    if (!((p.type == 'S' && !in) || (p.type == 'C' && in))) return;
    if (target->second == "ASUS") {
        ++stats.asus;
        if (n >= 2 && data[0] == 0xEC && data[1] == 0x40) {
            if (n >= 65) ++stats.asusEc40Full;
            else ++stats.asusEc40Short;
        }
    } else if (target->second == "DEEP") {
        ++stats.deep;
    }
}
static void printStats(const TrafficStats& stats) {
    // Put route diagnostics first so they remain visible in the Qt status label.
    std::cout << "STATS ASUS_RAW=" << stats.asusRaw
              << " ASUS_CTRL_OUT=" << stats.asusCtrlOut
              << " ASUS_CTRL_IN=" << stats.asusCtrlIn
              << " ASUS_SETREPORT=" << stats.asusSetReport
              << " ASUS_EXACT_EC40=" << stats.asusSetReportEc40
              << " ASUS_USB=" << stats.asus
              << " EC40_FULL=" << stats.asusEc40Full
              << " EC40_SHORT=" << stats.asusEc40Short
              << " DEEP_USB=" << stats.deep
              << " READ_ERRORS=" << stats.readErrors
              << '\n' << std::flush;
}
static void fetchOne(int fd, const TargetMap& targets, TrafficStats& stats) {
    MonPacket packet{};
    std::array<std::uint8_t, 4096> data{};
    MonGetArg req{&packet, data.data(), data.size()};
    if (::ioctl(fd, MON_IOCX_GETX, &req) < 0) {
        if (errno != EAGAIN && errno != EINTR) {
            ++stats.readErrors;
            std::cout << "STATE READ_ERROR " << std::strerror(errno) << '\n' << std::flush;
        }
        return;
    }
    const std::size_t n = std::min<std::size_t>(packet.len_cap, data.size());
    countTraffic(stats, packet, data.data(), n, targets);
    const auto target = targets.find({packet.busnum, packet.devnum});
    if (target != targets.end() && target->second == "ASUS") {
        const bool controlOut = packet.type == 'S' &&
            packet.xfer_type == 2 && packet.epnum == 0;
        const bool firstAny = stats.asusFirstSamples++ == 0;
        const bool firstControlOut = controlOut &&
            stats.asusFirstOutSamples++ == 0;
        if (firstAny || firstControlOut) {
            std::cout << "STATE ASUS_SAMPLE type=" << char(packet.type)
                      << " xfer=" << unsigned(packet.xfer_type)
                      << " ep=" << unsigned(packet.epnum)
                      << " bus=" << packet.busnum << " dev=" << unsigned(packet.devnum)
                      << " captured=" << n;
            if (controlOut) {
                std::cout << " setup=" << hex(packet.setup, 8)
                          << " payload=" << (n ? hex(data.data(), std::min<std::size_t>(n, 5)) : "none");
            }
            std::cout << '\n' << std::flush;
        }
    }
    const auto formatted = formatEvent(packet, data.data(), n, targets);
    if (!formatted.empty()) std::cout << formatted << '\n' << std::flush;
}
static bool selftest() {
    TargetMap fake{{{1, 3}, "ASUS"}, {{1, 5}, "DEEP"}};
    MonPacket m{};
    m.type = 'S'; m.busnum = 1; m.devnum = 3; m.epnum = 0;
    std::array<std::uint8_t, 65> frame{};
    frame[0] = 0xEC; frame[1] = 0x40; frame[2] = 0x00;
    frame[3] = 0x00; frame[4] = 0x14;
    const auto a = formatEvent(m, frame.data(), frame.size(), fake);
    m.busnum = 1; m.devnum = 5; m.epnum = 2;
    const std::array<std::uint8_t, 4> cmd{0xAA, 0x2E, 0x10, 0x00};
    const auto b = formatEvent(m, cmd.data(), cmd.size(), fake);
    m.type = 'C'; m.epnum = 0x82;
    const std::array<std::uint8_t, 4> ack{0x55, 0x2E, 0x10, 0x00};
    const auto c = formatEvent(m, ack.data(), ack.size(), fake);
    TrafficStats stats{};
    countTraffic(stats, m, ack.data(), ack.size(), fake);
    m.type = 'S'; m.xfer_type = 2; m.busnum = 1; m.devnum = 3; m.epnum = 0;
    const std::uint8_t setup[8] = {0x21,0x09,0xEC,0x02,0x02,0x00,0x41,0x00};
    std::memcpy(m.setup, setup, sizeof(setup));
    countTraffic(stats, m, frame.data(), frame.size(), fake);
    if (stats.asus != 1 || stats.asusEc40Full != 1 ||
        stats.asusCtrlOut != 1 || stats.asusSetReport != 1 ||
        stats.asusSetReportEc40 != 1 || stats.asusRaw != 1 || stats.deep != 1 ||
        a.find("FRAME ") == std::string::npos ||
        a.find("EC40") == std::string::npos ||
        b.find("EP2 HIDC-10") == std::string::npos ||
        c.find("IN  EP2 HIDC-10") == std::string::npos) {
        std::cerr << "SELFTEST FAILED\n";
        return false;
    }
    std::cout << "SELFTEST OK: 65-byte EC40, DeepCool OUT/IN, USB endpoint mapping\n";
    std::cout << "SELFTEST NOTE: synthetic test, NOT live motherboard colors\n";
    return true;
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--selftest") return selftest() ? 0 : 1;
    if (argc != 1) { std::cerr << "Usage: " << argv[0] << " [--selftest]\n"; return 2; }
    std::vector<Monitor> monitors;
    TargetMap targets = discover();
    bool active = false;
    TrafficStats traffic;
    std::string input;
    auto lastRefresh = std::chrono::steady_clock::now();
    auto lastStats = std::chrono::steady_clock::now();
    std::cout << "STATE READY - waiting for START; no root required for launch\n" << std::flush;
    while (true) {
        std::vector<pollfd> polls{{STDIN_FILENO, POLLIN, 0}};
        if (active) for (const auto& m : monitors) polls.push_back({m.fd, POLLIN, 0});
        const int result = ::poll(polls.data(), polls.size(), 250);
        if (result < 0 && errno != EINTR) {
            std::cout << "STATE POLL_ERROR " << std::strerror(errno) << '\n' << std::flush;
            break;
        }
        if (!polls.empty() && (polls[0].revents & (POLLIN | POLLHUP))) {
            char buffer[512];
            const ssize_t n = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n <= 0) break;
            input.append(buffer, static_cast<std::size_t>(n));
            std::size_t newline = 0;
            while ((newline = input.find('\n')) != std::string::npos) {
                std::string cmd = input.substr(0, newline);
                input.erase(0, newline + 1);
                while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == ' ')) cmd.pop_back();
                std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char ch) { return std::toupper(ch); });
                if (cmd == "QUIT") {
                    closeAll(monitors);
                    std::cout << "STATE STOPPED\n" << std::flush;
                    return 0;
                }
                if (cmd == "PAUSE") {
                    active = false;
                    closeAll(monitors);
                    std::cout << "STATE PAUSED\n" << std::flush;
                } else if (cmd == "START") {
                    targets = discover();
                    traffic = TrafficStats{};
                    printTargets(targets);
                    active = startMonitors(monitors, targets);
                    lastStats = std::chrono::steady_clock::now();
                } else if (!cmd.empty()) {
                    std::cout << "STATE INVALID_COMMAND\n" << std::flush;
                }
            }
        }
        // Device addresses change on reconnect: refresh VID:PID matches periodically.
        if (active && std::chrono::steady_clock::now() - lastRefresh > std::chrono::seconds(3)) {
            TargetMap updated = discover();
            if (updated != targets) {
                targets = std::move(updated);
                printTargets(targets);
                active = startMonitors(monitors, targets);
            }
            lastRefresh = std::chrono::steady_clock::now();
        }
        if (active && result > 0) {
            for (std::size_t i = 1; i < polls.size() && i-1 < monitors.size(); ++i) {
                if (polls[i].revents & POLLIN)
                    fetchOne(monitors[i-1].fd, targets, traffic);
                if (polls[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    std::cout << "STATE MONITOR_DISCONNECTED\n" << std::flush;
                    active = false;
                    closeAll(monitors);
                    break;
                }
            }
        }
        if (active && std::chrono::steady_clock::now() - lastStats >=
                          std::chrono::seconds(5)) {
            printStats(traffic);
            lastStats = std::chrono::steady_clock::now();
        }
    }
    closeAll(monitors);
    return 0;
}
