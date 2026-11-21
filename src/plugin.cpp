// SPDX-FileCopyrightText: 2026 CinnaWM Contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "plugin.h"
#include "x11_keystate.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QListView>
#include <QModelIndex>
#include <QPlainTextEdit>
#include <QSettings>
#include <QWidget>
#include <albert/app.h>
#include <albert/icon.h>
#include <albert/matcher.h>
#include <albert/rankitem.h>
#include <albert/standarditem.h>
#include <albert/widgetsutil.h>
#include <algorithm>

using namespace Qt::StringLiterals;

ALBERT_LOGGING_CATEGORY("cinnawm_switcher")

namespace cinnawm {
namespace {

constexpr auto kCfgIncludeMinimized = "include_minimized";
constexpr auto kCfgCurrentWorkspaceFirst = "current_workspace_first";

bool fetchOpenWindowsDbus(const DbusConfig &cfg,
                          std::vector<WindowDescriptor> &outWindows,
                          QString &errorOut)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        cfg.busName,
        cfg.objectPath,
        cfg.interfaceName,
        u"GetOpenWindows"_s);

    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, cfg.timeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        errorOut = reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
        return false;
    }
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        errorOut = u"Unexpected reply from %1"_s.arg(cfg.busName);
        return false;
    }

    const QByteArray payload = reply.arguments().at(0).toString().toUtf8();
    return parseOpenWindowsJson(payload, outWindows, &errorOut);
}

bool callWindowIntMethodDbus(const DbusConfig &cfg, const QString &method, int winId)
{
    if (winId <= 0) {
        return false;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(
        cfg.busName,
        cfg.objectPath,
        cfg.interfaceName,
        method);
    msg << static_cast<qint32>(winId);

    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, cfg.timeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        WARN << method << "(" << winId << ") failed:" << reply.errorMessage();
        return false;
    }
    if (reply.arguments().isEmpty()) {
        return false;
    }
    return reply.arguments().at(0).toBool();
}

std::unique_ptr<albert::Icon> createWindowIcon(const QString &iconRef, const QString &wmClass)
{
    if (!iconRef.isEmpty()) {
        const QFileInfo fi(iconRef);
        if (fi.isAbsolute() && fi.exists()) {
            return albert::Icon::image(iconRef);
        }
        if (QIcon::hasThemeIcon(iconRef)) {
            return albert::Icon::theme(iconRef);
        }
    }

    if (!wmClass.isEmpty()) {
        const QString lower = wmClass.toLower();
        QString dashed = lower;
        dashed.replace(u' ', u'-');
        const QString candidates[] = {wmClass, lower, dashed};
        for (const QString &cand : candidates) {
            if (!cand.isEmpty() && QIcon::hasThemeIcon(cand)) {
                return albert::Icon::theme(cand);
            }
        }
    }

    return albert::Icon::standard(albert::Icon::DesktopIcon);
}

QListView *findResultsListView(QWidget *root)
{
    if (!root) {
        return nullptr;
    }
    const auto views = root->findChildren<QListView *>();
    QListView *fallback = nullptr;
    for (QListView *view : views) {
        const QString cls = QString::fromLatin1(view->metaObject()->className());
        if (cls.contains(u"Action"_s, Qt::CaseInsensitive)) {
            continue;
        }
        if (view->isVisible()) {
            return view;
        }
        if (!fallback) {
            fallback = view;
        }
    }
    return fallback;
}

}  // namespace

SwitcherDbusBridge::SwitcherDbusBridge(Plugin *owner)
    : QObject(owner),
      m_owner(owner)
{
}

bool SwitcherDbusBridge::cycleWindow()
{
    return m_owner ? m_owner->cycleWindow(1) : false;
}

bool SwitcherDbusBridge::stepBackward()
{
    return m_owner ? m_owner->cycleWindow(-1) : false;
}

Plugin::Plugin()
    : m_dbusConfig(loadDbusConfigFromEnv()),
      m_iconIndex(std::make_shared<DesktopIconIndex>()),
      m_dbusBridge(new SwitcherDbusBridge(this))
{
    m_superWatchTimer.setInterval(25);
    connect(&m_superWatchTimer, &QTimer::timeout, this, &Plugin::onSuperWatchTick);
}

Plugin::~Plugin()
{
    m_superWatchTimer.stop();
    if (m_dbusRegistered) {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(m_dbusConfig.switcherObjectPath);
        bus.unregisterService(m_dbusConfig.switcherBusName);
        m_dbusRegistered = false;
    }
}

void Plugin::initialize()
{
    if (auto s = settings()) {
        m_includeMinimized = s->value(QLatin1StringView(kCfgIncludeMinimized), true).toBool();
        m_currentWorkspaceFirst = s->value(QLatin1StringView(kCfgCurrentWorkspaceFirst), true).toBool();
    }

    auto bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        const bool objOk = bus.registerObject(
            m_dbusConfig.switcherObjectPath,
            m_dbusBridge,
            QDBusConnection::ExportScriptableSlots);
        const bool svcOk = bus.registerService(m_dbusConfig.switcherBusName);
        m_dbusRegistered = objOk && svcOk;
        if (!m_dbusRegistered) {
            WARN << "Failed to register switcher D-Bus endpoint:" << bus.lastError().message();
        }
    }

    ExtensionPlugin::initialize();
}

bool Plugin::includeMinimized() const
{
    return m_includeMinimized;
}

void Plugin::setIncludeMinimized(bool enabled)
{
    if (m_includeMinimized == enabled) {
        return;
    }
    m_includeMinimized = enabled;
    if (auto s = settings()) {
        s->setValue(QLatin1StringView(kCfgIncludeMinimized), enabled);
    }
    emit includeMinimizedChanged(enabled);
}

bool Plugin::currentWorkspaceFirst() const
{
    return m_currentWorkspaceFirst;
}

void Plugin::setCurrentWorkspaceFirst(bool enabled)
{
    if (m_currentWorkspaceFirst == enabled) {
        return;
    }
    m_currentWorkspaceFirst = enabled;
    if (auto s = settings()) {
        s->setValue(QLatin1StringView(kCfgCurrentWorkspaceFirst), enabled);
    }
    emit currentWorkspaceFirstChanged(enabled);
}

QWidget *Plugin::buildConfigWidget()
{
    auto *widget = new QWidget();
    auto *layout = new QFormLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(8);
    layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *incMinCheck = new QCheckBox(widget);
    albert::bindWidget(incMinCheck, this, &Plugin::includeMinimized, &Plugin::setIncludeMinimized, &Plugin::includeMinimizedChanged);
    layout->addRow(u"Include minimized windows"_s, incMinCheck);

    auto *activeWsCheck = new QCheckBox(widget);
    albert::bindWidget(activeWsCheck, this, &Plugin::currentWorkspaceFirst, &Plugin::setCurrentWorkspaceFirst, &Plugin::currentWorkspaceFirstChanged);
    layout->addRow(u"List active workspace windows first"_s, activeWsCheck);

    return widget;
}

QString Plugin::defaultTrigger() const
{
    return m_dbusConfig.trigger;
}

QString Plugin::synopsis(const QString &) const
{
    return u"<title, app, ws1, tab, pip, scratchpad>"_s;
}

bool Plugin::supportsFuzzyMatching() const
{
    return true;
}

void Plugin::setFuzzyMatching(bool enabled)
{
    m_fuzzy = enabled;
}

bool Plugin::isSuperCurrentlyHeld() const
{
    if (auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr) {
        if (Display *dpy = x11->display()) {
            return isSuperKeyHeld(dpy);
        }
    }
    return QGuiApplication::queryKeyboardModifiers().testFlag(Qt::MetaModifier);
}

void Plugin::attachFrontendWidgets()
{
    QWidget *foundWindow = nullptr;
    QListView *foundList = nullptr;
    QPlainTextEdit *foundInput = nullptr;

    const auto topWidgets = QApplication::topLevelWidgets();
    for (QWidget *w : topWidgets) {
        if (!w || qobject_cast<QDialog *>(w)) {
            continue;
        }
        auto *input = w->findChild<QPlainTextEdit *>();
        auto *list = findResultsListView(w);
        if (input && list) {
            foundWindow = w;
            foundList = list;
            foundInput = input;
            if (w->isVisible()) {
                break;
            }
        }
    }

    if (m_frontendWindow != foundWindow) {
        if (m_frontendWindow) {
            m_frontendWindow->removeEventFilter(this);
        }
        m_frontendWindow = foundWindow;
        if (m_frontendWindow) {
            m_frontendWindow->installEventFilter(this);
        }
    }

    if (m_inputEdit != foundInput) {
        if (m_inputEdit) {
            m_inputEdit->removeEventFilter(this);
        }
        m_inputEdit = foundInput;
        if (m_inputEdit) {
            m_inputEdit->installEventFilter(this);
        }
    }

    if (m_resultsList != foundList) {
        if (m_resultsList) {
            m_resultsList->removeEventFilter(this);
        }
        m_resultsList = foundList;
        if (m_resultsList) {
            m_resultsList->installEventFilter(this);
        }
    }

    QAbstractItemModel *model = m_resultsList ? m_resultsList->model() : nullptr;
    if (m_connectedModel != model) {
        disconnect(m_rowsInsertedConn);
        disconnect(m_modelResetConn);
        m_connectedModel = model;
        if (m_connectedModel) {
            m_rowsInsertedConn = connect(
                m_connectedModel,
                &QAbstractItemModel::rowsInserted,
                this,
                [this]() { applyPendingSteps(); });
            m_modelResetConn = connect(
                m_connectedModel,
                &QAbstractItemModel::modelReset,
                this,
                [this]() { applyPendingSteps(); });
        }
    }
}

void Plugin::stepSelection(int delta)
{
    attachFrontendWidgets();
    QAbstractItemModel *model = m_resultsList ? m_resultsList->model() : nullptr;
    const int rowCount = model ? model->rowCount() : 0;
    if (!m_resultsList || !model || rowCount <= 0) {
        m_pendingSteps += delta;
        return;
    }

    const QModelIndex curIdx = m_resultsList->currentIndex();
    const int curRow = curIdx.isValid() ? curIdx.row() : 0;
    const int nextRow = ((curRow + delta) % rowCount + rowCount) % rowCount;
    const QModelIndex nextIdx = model->index(nextRow, 0);
    if (nextIdx.isValid()) {
        m_resultsList->setCurrentIndex(nextIdx);
        m_resultsList->scrollTo(nextIdx);
    }
}

void Plugin::applyPendingSteps()
{
    if (!m_cycleActive || m_pendingSteps == 0) {
        return;
    }
    const int steps = m_pendingSteps;
    m_pendingSteps = 0;
    stepSelection(steps);
}

void Plugin::activateSelectedRow()
{
    attachFrontendWidgets();
    if (!m_frontendWindow || !m_frontendWindow->isVisible() || !m_resultsList) {
        return;
    }
    const QModelIndex curIdx = m_resultsList->currentIndex();
    if (!curIdx.isValid()) {
        return;
    }

    if (m_inputEdit) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(m_inputEdit, &press);
        QApplication::sendEvent(m_inputEdit, &release);
    }
    if (m_frontendWindow && m_frontendWindow->isVisible()) {
        emit m_resultsList->activated(curIdx);
    }
}

void Plugin::resetCycleState()
{
    m_superWatchTimer.stop();
    m_cycleActive = false;
    m_cyclePressCount = 0;
    m_pendingSteps = 0;
}

void Plugin::onSuperWatchTick()
{
    if (!m_cycleActive) {
        m_superWatchTimer.stop();
        return;
    }
    if (!m_frontendWindow || !m_frontendWindow->isVisible()) {
        resetCycleState();
        return;
    }
    if (isSuperCurrentlyHeld()) {
        return;
    }

    m_superWatchTimer.stop();
    if (m_cyclePressCount >= 2) {
        m_cycleActive = false;
        m_cyclePressCount = 0;
        m_pendingSteps = 0;
        activateSelectedRow();
    } else {
        m_cyclePressCount = 0;
    }
}

bool Plugin::cycleWindow(int delta)
{
    attachFrontendWidgets();
    const bool alreadyVisible = m_frontendWindow && m_frontendWindow->isVisible();
    const QString curText = m_inputEdit ? m_inputEdit->toPlainText() : QString{};
    const bool onTrigger = curText.startsWith(m_dbusConfig.trigger);

    if (alreadyVisible && onTrigger) {
        m_cycleActive = true;
        m_cyclePressCount = std::max(2, m_cyclePressCount + 1);
        if (!m_superWatchTimer.isActive()) {
            m_superWatchTimer.start();
        }
        stepSelection(delta);
        return true;
    }

    m_cycleActive = true;
    m_cyclePressCount = 1;
    m_pendingSteps = (delta < 0) ? delta : 0;

    albert::app().show(m_dbusConfig.trigger);

    QTimer::singleShot(0, this, [this]() {
        attachFrontendWidgets();
        applyPendingSteps();
    });
    if (!m_superWatchTimer.isActive()) {
        m_superWatchTimer.start();
    }
    return true;
}

bool Plugin::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_frontendWindow && event->type() == QEvent::Hide) {
        resetCycleState();
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        const auto *ke = static_cast<QKeyEvent *>(event);
        const QString curText = m_inputEdit ? m_inputEdit->toPlainText() : QString{};
        const bool onTrigger = curText.startsWith(m_dbusConfig.trigger);

        if (m_frontendWindow && m_frontendWindow->isVisible() && onTrigger) {
            if (ke->key() == Qt::Key_Tab && (m_cycleActive || curText == m_dbusConfig.trigger)) {
                if (isSuperCurrentlyHeld()) {
                    m_cyclePressCount = std::max(2, m_cyclePressCount + 1);
                    if (!m_superWatchTimer.isActive()) {
                        m_superWatchTimer.start();
                    }
                }
                stepSelection(1);
                return true;
            }
            if (ke->key() == Qt::Key_Backtab && (m_cycleActive || curText == m_dbusConfig.trigger)) {
                if (isSuperCurrentlyHeld()) {
                    m_cyclePressCount = std::max(2, m_cyclePressCount + 1);
                    if (!m_superWatchTimer.isActive()) {
                        m_superWatchTimer.start();
                    }
                }
                stepSelection(-1);
                return true;
            }
            const int key = ke->key();
            if (!ke->text().isEmpty() && key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Escape) {
                resetCycleState();
            }
        }
    }

    return albert::ExtensionPlugin::eventFilter(watched, event);
}

std::vector<albert::RankItem> Plugin::rankItems(albert::QueryContext &context)
{
    if (!context.isValid()) {
        return {};
    }

    const QString trimmedQuery = context.query().trimmed();
    const bool isTriggered = !context.trigger().isEmpty();
    if (!isTriggered && trimmedQuery.isEmpty()) {
        return {};
    }

    std::vector<WindowDescriptor> windows;
    QString errMsg;
    if (!fetchOpenWindowsDbus(m_dbusConfig, windows, errMsg)) {
        if (!isTriggered) {
            return {};
        }
        auto errItem = albert::StandardItem::make(
            u"cinnawm-dbus-unavailable"_s,
            u"CinnaWM D-Bus service unavailable"_s,
            u"Enable CinnaWM in Cinnamon Extensions (%1)"_s.arg(errMsg),
            []() { return albert::Icon::standard(albert::Icon::MessageBoxWarning); });
        return {albert::RankItem{std::move(errItem), 1.0}};
    }

    if (!m_includeMinimized) {
        std::erase_if(windows, [](const WindowDescriptor &w) { return w.minimized; });
    }
    sortWindows(windows, m_currentWorkspaceFirst);

    const albert::MatchConfig matchCfg{.fuzzy = m_fuzzy};
    const albert::Matcher matcher(trimmedQuery, matchCfg);

    std::vector<albert::RankItem> results;
    results.reserve(windows.size());

    const size_t total = windows.size();
    for (size_t idx = 0; idx < total; ++idx) {
        if (!context.isValid()) {
            return {};
        }

        const WindowDescriptor &win = windows[idx];
        const QString iconRef = m_iconIndex ? m_iconIndex->resolve(win.wmClass) : QString{};
        const WindowItemMeta meta = formatWindowItem(win, iconRef);

        double score = 0.0;
        if (trimmedQuery.isEmpty()) {
            score = std::max(0.01, 1.0 - (static_cast<double>(idx) * 0.001));
        } else {
            const albert::Match m = matcher.match(meta.text, meta.wmClass, meta.searchableSummary);
            if (!m.isMatch()) {
                continue;
            }
            score = std::max(0.01, m.score());
        }

        const DbusConfig dbusCfg = m_dbusConfig;
        const int winId = meta.id;

        std::vector<albert::Action> actions;
        actions.push_back(albert::Action{
            u"switch"_s,
            meta.actionLabel,
            [dbusCfg, winId]() { callWindowIntMethodDbus(dbusCfg, u"ActivateWindow"_s, winId); },
            true});
        actions.push_back(albert::Action{
            u"close"_s,
            u"Close Window"_s,
            [dbusCfg, winId]() { callWindowIntMethodDbus(dbusCfg, u"CloseWindow"_s, winId); },
            true});

        auto item = albert::StandardItem::make(
            u"cinnawm-win-%1"_s.arg(winId),
            meta.text,
            meta.subtext,
            [capturedIcon = meta.iconRef, capturedClass = meta.wmClass]() {
                return createWindowIcon(capturedIcon, capturedClass);
            },
            std::move(actions));

        results.emplace_back(std::move(item), score);
    }

    return results;
}

}  // namespace cinnawm
