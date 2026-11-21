// SPDX-FileCopyrightText: 2026 CinnaWM Contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "cinnawm_core.h"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <albert/extensionplugin.h>
#include <albert/globalqueryhandler.h>
#include <albert/logging.h>
#include <memory>
#include <vector>

class QAbstractItemModel;
class QEvent;
class QListView;
class QPlainTextEdit;
class QWidget;

namespace cinnawm {

class Plugin;

class SwitcherDbusBridge final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.cinnawm.AlbertSwitcher")

public:
    explicit SwitcherDbusBridge(Plugin *owner);

public slots:
    Q_SCRIPTABLE bool cycleWindow();
    Q_SCRIPTABLE bool stepBackward();

private:
    Plugin *m_owner;
};

class Plugin final : public albert::ExtensionPlugin,
                     public albert::GlobalQueryHandler {
    ALBERT_PLUGIN

public:
    Plugin();
    ~Plugin() override;

    void initialize() override;
    QWidget *buildConfigWidget() override;

    QString defaultTrigger() const override;
    QString synopsis(const QString &query) const override;
    bool supportsFuzzyMatching() const override;
    void setFuzzyMatching(bool enabled) override;

    std::vector<albert::RankItem> rankItems(albert::QueryContext &context) override;

    bool includeMinimized() const;
    void setIncludeMinimized(bool enabled);

    bool currentWorkspaceFirst() const;
    void setCurrentWorkspaceFirst(bool enabled);

    bool cycleWindow(int delta = 1);

signals:
    void includeMinimizedChanged(bool enabled);
    void currentWorkspaceFirstChanged(bool enabled);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void attachFrontendWidgets();
    void stepSelection(int delta);
    void applyPendingSteps();
    void activateSelectedRow();
    void resetCycleState();
    void onSuperWatchTick();
    bool isSuperCurrentlyHeld() const;

    bool m_includeMinimized = true;
    bool m_currentWorkspaceFirst = true;
    bool m_fuzzy = false;
    DbusConfig m_dbusConfig;
    std::shared_ptr<DesktopIconIndex> m_iconIndex;

    SwitcherDbusBridge *m_dbusBridge = nullptr;
    bool m_dbusRegistered = false;

    bool m_cycleActive = false;
    int m_cyclePressCount = 0;
    int m_pendingSteps = 0;
    QTimer m_superWatchTimer;

    QPointer<QWidget> m_frontendWindow;
    QPointer<QListView> m_resultsList;
    QPointer<QPlainTextEdit> m_inputEdit;
    QPointer<QAbstractItemModel> m_connectedModel;
    QMetaObject::Connection m_rowsInsertedConn;
    QMetaObject::Connection m_modelResetConn;
};

}  // namespace cinnawm
