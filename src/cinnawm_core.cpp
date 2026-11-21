// SPDX-FileCopyrightText: 2026 CinnaWM Contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "cinnawm_core.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

namespace cinnawm {

int parseTimeoutMs(const QString &rawValue, int fallback)
{
    bool ok = false;
    const int parsed = rawValue.trimmed().toInt(&ok);
    return (ok && parsed >= 100) ? parsed : fallback;
}

DbusConfig loadDbusConfigFromEnv()
{
    auto envOr = [](const char *varName, const QString &def) -> QString {
        const QString val = qEnvironmentVariable(varName);
        return val.isEmpty() ? def : val;
    };

    DbusConfig cfg;
    cfg.busName = envOr("CINNAWM_DBUS_NAME", u"org.cinnawm.Service"_s);
    cfg.objectPath = envOr("CINNAWM_DBUS_PATH", u"/org/cinnawm/Service"_s);
    cfg.interfaceName = envOr("CINNAWM_DBUS_INTERFACE", u"org.cinnawm.Service"_s);
    cfg.trigger = envOr("CINNAWM_ALBERT_TRIGGER", u"w "_s);
    cfg.switcherBusName = envOr("CINNAWM_ALBERT_DBUS_NAME", u"org.cinnawm.AlbertSwitcher"_s);
    cfg.switcherObjectPath = envOr("CINNAWM_ALBERT_DBUS_PATH", u"/org/cinnawm/AlbertSwitcher"_s);
    cfg.timeoutMs = parseTimeoutMs(qEnvironmentVariable("CINNAWM_DBUS_TIMEOUT_MS"), 1500);
    return cfg;
}

bool parseOpenWindowsJson(const QByteArray &jsonBytes,
                          std::vector<WindowDescriptor> &outWindows,
                          QString *errorOut)
{
    outWindows.clear();
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes.isEmpty() ? QByteArrayLiteral("[]") : jsonBytes, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        if (errorOut) {
            *errorOut = u"Invalid JSON from CinnaWM: %1"_s.arg(parseErr.errorString());
        }
        return false;
    }
    if (!doc.isArray()) {
        if (errorOut) {
            *errorOut = u"Unexpected window payload from CinnaWM"_s;
        }
        return false;
    }

    const QJsonArray arr = doc.array();
    outWindows.reserve(static_cast<size_t>(arr.size()));
    for (const QJsonValue &val : arr) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject obj = val.toObject();
        const QJsonValue idVal = obj.value(u"id"_s);
        if (!idVal.isDouble()) {
            continue;
        }
        WindowDescriptor desc;
        desc.id = idVal.toInt();
        desc.title = obj.value(u"title"_s).toString();
        desc.wmClass = obj.value(u"wmClass"_s).toString();
        desc.workspace = obj.value(u"workspace"_s).toInt(-1);
        desc.onActiveWorkspace = obj.value(u"onActiveWorkspace"_s).toBool(false);
        desc.monitor = obj.value(u"monitor"_s).toInt(-1);
        desc.focused = obj.value(u"focused"_s).toBool(false);
        desc.minimized = obj.value(u"minimized"_s).toBool(false);
        desc.floating = obj.value(u"floating"_s).toBool(false);
        desc.stacked = obj.value(u"stacked"_s).toBool(false);
        desc.scratchpad = obj.value(u"scratchpad"_s).toBool(false);
        desc.pip = obj.value(u"pip"_s).toBool(false);
        outWindows.push_back(std::move(desc));
    }
    return true;
}

void sortWindows(std::vector<WindowDescriptor> &windows, bool currentWorkspaceFirst)
{
    std::stable_sort(windows.begin(), windows.end(), [currentWorkspaceFirst](const WindowDescriptor &a, const WindowDescriptor &b) {
        const auto key = [currentWorkspaceFirst](const WindowDescriptor &w) {
            const int focusedRank = w.focused ? 1 : 0;
            const int wsRank = (currentWorkspaceFirst && !w.onActiveWorkspace) ? 1 : 0;
            const int minRank = w.minimized ? 1 : 0;
            const int wsNum = (w.workspace >= 0) ? w.workspace : 999;
            return std::tuple<int, int, int, int, int>{focusedRank, wsRank, minRank, wsNum, w.id};
        };
        return key(a) < key(b);
    });
}

void buildWindowTags(const WindowDescriptor &win, QStringList &displayTags, QStringList &searchTokens)
{
    displayTags.clear();
    searchTokens.clear();

    if (win.focused) {
        displayTags.append(u"★ Focused"_s);
        searchTokens.append(u"focused"_s);
    }

    if (win.workspace >= 0) {
        const QString wsNum = QString::number(win.workspace + 1);
        displayTags.append(u"WS %1"_s.arg(wsNum));
        searchTokens.append(u"ws%1"_s.arg(wsNum));
        searchTokens.append(u"ws %1"_s.arg(wsNum));
    } else {
        displayTags.append(u"All WS"_s);
        searchTokens.append(u"all"_s);
        searchTokens.append(u"sticky"_s);
    }

    if (win.monitor >= 0) {
        const QString monNum = QString::number(win.monitor + 1);
        displayTags.append(u"Mon %1"_s.arg(monNum));
        searchTokens.append(u"mon%1"_s.arg(monNum));
        searchTokens.append(u"mon %1"_s.arg(monNum));
    }

    if (win.pip) {
        displayTags.append(u"PiP"_s);
        searchTokens.append(u"pip"_s);
        searchTokens.append(u"sticky"_s);
    }
    if (win.scratchpad) {
        displayTags.append(u"Scratchpad"_s);
        searchTokens.append(u"scratchpad"_s);
        searchTokens.append(u"scratch"_s);
    }
    if (win.stacked) {
        displayTags.append(u"Tab"_s);
        searchTokens.append(u"tab"_s);
        searchTokens.append(u"stacked"_s);
    }
    if (win.minimized) {
        displayTags.append(u"Min"_s);
        searchTokens.append(u"minimized"_s);
        searchTokens.append(u"min"_s);
    }

    if (!win.pip && !win.scratchpad) {
        if (win.floating) {
            displayTags.append(u"Float"_s);
            searchTokens.append(u"floating"_s);
            searchTokens.append(u"float"_s);
        } else {
            displayTags.append(u"Tile"_s);
            searchTokens.append(u"tile"_s);
            searchTokens.append(u"tiling"_s);
        }
    }
}

WindowItemMeta formatWindowItem(const WindowDescriptor &win, const QString &resolvedIcon)
{
    const QString cleanClass = win.wmClass.trimmed();
    const QString collapsedTitle = win.title.simplified();
    const QString title = !collapsedTitle.isEmpty()
                              ? collapsedTitle
                              : (!cleanClass.isEmpty() ? cleanClass : u"Window #%1"_s.arg(win.id));

    QStringList displayTags;
    QStringList searchTokens;
    buildWindowTags(win, displayTags, searchTokens);

    const QString appLabel = !cleanClass.isEmpty() ? cleanClass : u"Window"_s;
    const QString subtext = u"%1  •  %2"_s.arg(appLabel, displayTags.join(u" · "_s));

    QString actionLabel = u"Switch to Window"_s;
    if (win.scratchpad) {
        actionLabel = u"Show from Scratchpad"_s;
    } else if (win.stacked) {
        actionLabel = u"Switch to Tab"_s;
    } else if (win.minimized) {
        actionLabel = u"Unminimize & Switch"_s;
    }

    QStringList summaryParts;
    summaryParts.reserve(3);
    summaryParts.append(title);
    if (!cleanClass.isEmpty()) {
        summaryParts.append(cleanClass);
    }
    if (!searchTokens.isEmpty()) {
        summaryParts.append(searchTokens.join(u' '));
    }

    WindowItemMeta meta;
    meta.id = win.id;
    meta.text = title;
    meta.subtext = subtext;
    meta.wmClass = cleanClass;
    meta.iconRef = resolvedIcon;
    meta.actionLabel = actionLabel;
    meta.searchableSummary = summaryParts.join(u' ');
    return meta;
}

bool parseDesktopEntryIcon(const QString &desktopFilePath, QString &iconOut, QString &startupWmOut)
{
    iconOut.clear();
    startupWmOut.clear();

    QFile file(desktopFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    bool inDesktopEntry = false;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        if (line.startsWith(u'[')) {
            if (inDesktopEntry) {
                break;
            }
            inDesktopEntry = (line == u"[Desktop Entry]"_s);
            continue;
        }
        if (!inDesktopEntry) {
            continue;
        }
        const qsizetype eqIdx = line.indexOf(u'=');
        if (eqIdx <= 0) {
            continue;
        }
        const QString key = line.left(eqIdx).trimmed();
        const QString val = line.mid(eqIdx + 1).trimmed();
        if (key == u"Icon"_s && iconOut.isEmpty()) {
            iconOut = val;
        } else if (key == u"StartupWMClass"_s && startupWmOut.isEmpty()) {
            startupWmOut = val;
        }
        if (!iconOut.isEmpty() && !startupWmOut.isEmpty()) {
            break;
        }
    }
    return !iconOut.isEmpty();
}

DesktopIconIndex::DesktopIconIndex(Provider provider, std::chrono::seconds ttl)
    : m_provider(std::move(provider)),
      m_ttl(ttl)
{
}

QHash<QString, QString> DesktopIconIndex::scanXdgApplications(const QStringList &customDirs)
{
    QHash<QString, QString> index;
    const QStringList dirs = !customDirs.isEmpty()
                                 ? customDirs
                                 : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);

    for (const QString &dirPath : dirs) {
        const QDir dir(dirPath);
        if (!dir.exists()) {
            continue;
        }
        QDirIterator it(dirPath, {u"*.desktop"_s}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString filePath = it.next();
            QString icon;
            QString startupWm;
            if (!parseDesktopEntryIcon(filePath, icon, startupWm) || icon.isEmpty()) {
                continue;
            }
            if (!startupWm.isEmpty()) {
                const QString wmKey = startupWm.toLower();
                if (!index.contains(wmKey)) {
                    index.insert(wmKey, icon);
                }
            }
            const QString stem = QFileInfo(filePath).completeBaseName().toLower();
            if (!stem.isEmpty() && !index.contains(stem)) {
                index.insert(stem, icon);
            }
            const qsizetype dotIdx = stem.lastIndexOf(u'.');
            if (dotIdx >= 0 && dotIdx + 1 < stem.size()) {
                const QString tail = stem.mid(dotIdx + 1);
                if (!index.contains(tail)) {
                    index.insert(tail, icon);
                }
            }
        }
    }
    return index;
}

void DesktopIconIndex::refreshIfStaleLocked()
{
    const auto now = std::chrono::steady_clock::now();
    if (m_initialized && (now - m_lastRefresh) < m_ttl) {
        return;
    }
    m_index = m_provider ? m_provider() : scanXdgApplications();
    m_lastRefresh = now;
    m_initialized = true;
}

QString DesktopIconIndex::resolve(const QString &wmClass)
{
    const QString clean = wmClass.trimmed();
    if (clean.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    refreshIfStaleLocked();

    const QString lower = clean.toLower();
    if (const auto it = m_index.constFind(lower); it != m_index.constEnd()) {
        return it.value();
    }

    QString dashed = lower;
    dashed.replace(u' ', u'-');
    if (const auto it = m_index.constFind(dashed); it != m_index.constEnd()) {
        return it.value();
    }
    return dashed;
}

}  // namespace cinnawm
