// SPDX-FileCopyrightText: 2026 CinnaWM Contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <chrono>
#include <functional>
#include <vector>

namespace cinnawm {

struct DbusConfig {
    QString busName;
    QString objectPath;
    QString interfaceName;
    QString trigger;
    QString switcherBusName;
    QString switcherObjectPath;
    int timeoutMs = 1500;
};

struct WindowDescriptor {
    int id = 0;
    QString title;
    QString wmClass;
    int workspace = -1;
    bool onActiveWorkspace = false;
    int monitor = -1;
    bool focused = false;
    bool minimized = false;
    bool floating = false;
    bool stacked = false;
    bool scratchpad = false;
    bool pip = false;
};

struct WindowItemMeta {
    int id = 0;
    QString text;
    QString subtext;
    QString wmClass;
    QString iconRef;
    QString actionLabel;
    QString searchableSummary;
};

DbusConfig loadDbusConfigFromEnv();
int parseTimeoutMs(const QString &rawValue, int fallback = 1500);

bool parseOpenWindowsJson(const QByteArray &jsonBytes,
                          std::vector<WindowDescriptor> &outWindows,
                          QString *errorOut = nullptr);

void sortWindows(std::vector<WindowDescriptor> &windows, bool currentWorkspaceFirst = true);

void buildWindowTags(const WindowDescriptor &win, QStringList &displayTags, QStringList &searchTokens);
WindowItemMeta formatWindowItem(const WindowDescriptor &win, const QString &resolvedIcon = {});

bool parseDesktopEntryIcon(const QString &desktopFilePath, QString &iconOut, QString &startupWmOut);

class DesktopIconIndex {
public:
    using Provider = std::function<QHash<QString, QString>()>;

    explicit DesktopIconIndex(Provider provider = {},
                              std::chrono::seconds ttl = std::chrono::seconds(120));

    QString resolve(const QString &wmClass);
    static QHash<QString, QString> scanXdgApplications(const QStringList &customDirs = {});

private:
    void refreshIfStaleLocked();

    QMutex m_mutex;
    Provider m_provider;
    std::chrono::seconds m_ttl;
    QHash<QString, QString> m_index;
    std::chrono::steady_clock::time_point m_lastRefresh{};
    bool m_initialized = false;
};

}  // namespace cinnawm
