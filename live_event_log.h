#pragma once
// MYSTIQUE STEP 6D LIVE EVENT LOG v1
// Captures Qt log messages and explicit GUI statuses; no hardware I/O.
// Up to 50 pending entries and 50 visible text blocks, plus 400 chars per event.
#include <QDateTime>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <cstdio>

namespace MystiqueLiveLog {
struct State {
    QMutex mutex;
    QQueue<QString> pending;
    QtMessageHandler previous = nullptr;
    bool installed = false;
};

inline State& state() {
    static State s;
    return s;
}

// MYSTIQUE STEP 6F LOG SEVERITY v1
// Display-only categorisation. No hardware calls and no changes to Qt logging.
// Only known normal qWarning messages are downgraded to INFO.
inline QString displayedSeverity(const QString& original, const QString& message) {
    const bool status = original == QStringLiteral("STATUS");
    const bool debug = original == QStringLiteral("DEBUG");
    const bool warning = original == QStringLiteral("WARN");
    if (!status && !debug && !warning)
        return original; // Preserve INFO, ERROR and FATAL exactly.

    // Clearly failed operations must not be displayed as routine DEBUG/STATUS.
    if (message.contains(QStringLiteral("❌")) ||
        message.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
        message.contains(QStringLiteral("cannot save"), Qt::CaseInsensitive) ||
        message.contains(QStringLiteral("invalid generated UUID"), Qt::CaseInsensitive))
        return QStringLiteral("ERROR");

    // GUI status changes are normally informational, not a separate severity.
    if (status) {
        if (message.contains(QStringLiteral("no DeepCool MYSTIQUE detected"),
                             Qt::CaseInsensitive) ||
            message.contains(QStringLiteral("uncertain"), Qt::CaseInsensitive) ||
            message.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive) ||
            message.contains(QStringLiteral("blocked"), Qt::CaseInsensitive))
            return QStringLiteral("WARN");
        return QStringLiteral("INFO");
    }

    // Legacy drag/drop tracing uses qWarning even for successful actions.
    // Correct only two explicitly-known normal messages, not all GIF warnings.
    if (warning && message.contains(QStringLiteral("[DROP-DIAG]")) &&
        (message.contains(QStringLiteral("GifDropWidget bereikt")) ||
         message.contains(QStringLiteral("Callback voor slot"))))
        return QStringLiteral("INFO");

    if (debug && message.contains(QStringLiteral("⚠")))
        return QStringLiteral("WARN");

    return original; // Unrecognised warnings remain WARN, not falsely INFO.
}

inline void add(const QString& severity, const QString& value) {
    QString message = value;
    message.replace(QLatin1Char('\n'), QStringLiteral(" | "));
    message.replace(QLatin1Char('\r'), QLatin1Char(' '));
    if (message.size() > 400)
        message = message.left(400) + QStringLiteral("...");
    const QString line = QDateTime::currentDateTime().toString(
        QStringLiteral("HH:mm:ss")) + QStringLiteral(" [") +
        displayedSeverity(severity, message) + QStringLiteral("] ") + message;
    auto& s = state();
    QMutexLocker lock(&s.mutex);
    while (s.pending.size() >= 50)
        s.pending.dequeue();
    s.pending.enqueue(line);
}

inline void messageHandler(QtMsgType type, const QMessageLogContext& context,
                           const QString& message) {
    // Preserve the existing terminal log stream, including any earlier handler.
    auto previous = state().previous;
    if (previous) {
        previous(type, context, message);
    } else {
        const QByteArray printable = qFormatLogMessage(type, context, message).toLocal8Bit();
        std::fprintf(stderr, "%s\n", printable.constData());
    }
    const QString severity = type == QtWarningMsg ? QStringLiteral("WARN") :
        type == QtCriticalMsg ? QStringLiteral("ERROR") :
        type == QtFatalMsg ? QStringLiteral("FATAL") :
        type == QtDebugMsg ? QStringLiteral("DEBUG") : QStringLiteral("INFO");
    // STEP 6E: presentation-only correction for known successful GIF events.
    const bool knownSuccess = type == QtWarningMsg &&
        message.contains(QStringLiteral("[GIF-SELECT]")) &&
        (message.contains(QStringLiteral("Bestaande GIF activeren")) ||
         message.contains(QStringLiteral("Valid 0x08 ACK")) ||
         (message.contains(QStringLiteral("Mode OK: true")) &&
          message.contains(QStringLiteral("Selection ACK OK: true"))));
    add(knownSuccess ? QStringLiteral("INFO") : severity, message);
}

inline void install() {
    auto& s = state();
    if (s.installed) return;
    s.previous = qInstallMessageHandler(messageHandler);
    s.installed = true;
}

inline void uninstall() {
    auto& s = state();
    if (!s.installed) return;
    qInstallMessageHandler(s.previous);
    s.installed = false;
}

inline QStringList takePending() {
    QStringList result;
    auto& s = state();
    QMutexLocker lock(&s.mutex);
    while (!s.pending.isEmpty())
        result.append(s.pending.dequeue());
    return result;
}
} // namespace MystiqueLiveLog
