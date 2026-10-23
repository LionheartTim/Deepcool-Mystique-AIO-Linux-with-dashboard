#pragma once
// MYSTIQUE STEP 6A READ-ONLY DIAGNOSTICS v1
// Pure status formatting. NO USB transfers, SD requests, writes or sensor polling.
#include <QDateTime>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace MystiqueDiagnostics {

struct Snapshot {
    bool usbOpen = false;
    QString usbName;
    QString cpuVendor;
    QString cpuModel;
    QString gpuVendor;
    QStringList gpuIdentifiers;
    bool profileFromCache = false;
    QString cpuTemperatureSource;
    QString gpuTemperatureSource;
    QString gpuLoadSource;
    QString lastGuiStatus;
    bool pendingDeleteJournal = false;
    int uncertainUploads = 0;
    int uncertainDeletes = 0;
    int locallyRecordedGifs = 0;
};

inline QString readable(const QString& text) {
    return text.trimmed().isEmpty() ? QStringLiteral("N/A") : text.trimmed();
}

inline QString sourceStatus(const QString& path) {
    if (path.isEmpty()) return QStringLiteral("Not configured");
    // Check path presence only. No telemetry reads and no rescan.
    const bool present = QFileInfo::exists(path);
    return (present ? QStringLiteral("Present: ")
                    : QStringLiteral("Missing / inaccessible: ")) + path;
}

inline QString formatSnapshot(const Snapshot& s) {
    QStringList lines;
    lines << QStringLiteral("READ-ONLY DIAGNOSTICS")
          << QStringLiteral("Captured: ") + QDateTime::currentDateTime().toString(Qt::ISODate)
          << QStringLiteral("No extra USB request, sensor scan or SD command is performed.")
          << QString()
          << QStringLiteral("[USB CONNECTION]")
          << QStringLiteral("Connection: ") +
             (s.usbOpen ? QStringLiteral("Open") : QStringLiteral("Not connected"))
          << QStringLiteral("Device: ") + readable(s.usbName)
          << QStringLiteral("Target: DeepCool MYSTIQUE 360 ARGB (3633:0009)")
          << QStringLiteral("Note: an open USB handle does not prove SD-card health.")
          << QString()
          << QStringLiteral("[CACHED HARDWARE PROFILE]")
          << QStringLiteral("CPU vendor: ") + readable(s.cpuVendor)
          << QStringLiteral("CPU model: ") + readable(s.cpuModel)
          << QStringLiteral("GPU vendor: ") + readable(s.gpuVendor)
          << QStringLiteral("GPU PCI identifiers: ") +
             (s.gpuIdentifiers.isEmpty() ? QStringLiteral("N/A")
                                         : s.gpuIdentifiers.join(QStringLiteral(", ")))
          << QStringLiteral("Profile loaded from cache this run: ") +
             (s.profileFromCache ? QStringLiteral("Yes") : QStringLiteral("No (detected this run)"))
          << QString()
          << QStringLiteral("[ACTIVE SENSOR PATHS]")
          << QStringLiteral("CPU temperature: ") + sourceStatus(s.cpuTemperatureSource)
          << QStringLiteral("GPU temperature: ") + sourceStatus(s.gpuTemperatureSource)
          << QStringLiteral("GPU load: ") + sourceStatus(s.gpuLoadSource)
          << QStringLiteral("Note: path presence does not validate sensor accuracy.")
          << QString()
          << QStringLiteral("[LOCAL GIF SAFETY STATE]")
          << QStringLiteral("Locally recorded uploads: ") + QString::number(s.locallyRecordedGifs)
          << QStringLiteral("Uncertain upload markers: ") + QString::number(s.uncertainUploads)
          << QStringLiteral("Uncertain delete markers: ") + QString::number(s.uncertainDeletes)
          << QStringLiteral("Delete recovery journal: ") +
             (s.pendingDeleteJournal ? QStringLiteral("Present - review required")
                                     : QStringLiteral("Not present"))
          << QStringLiteral("Note: local markers do NOT verify files on the TFT SD card.")
          << QString()
          << QStringLiteral("[LAST GUI STATUS]")
          << readable(s.lastGuiStatus)
          << QStringLiteral("This is the current status label, not a historical error log.")
          << QString()
          << QStringLiteral("[SD-CARD DIAGNOSTICS]")
          << QStringLiteral("Not checked: no validated safe read-only SD protocol in use.")
          << QStringLiteral("No reset, format, delete or write commands are available here.");
    return lines.join(QLatin1Char('\n'));
}

} // namespace MystiqueDiagnostics
