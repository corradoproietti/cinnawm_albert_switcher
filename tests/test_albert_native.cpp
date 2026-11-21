// SPDX-FileCopyrightText: 2026 CinnaWM Contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "../src/cinnawm_core.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace Qt::StringLiterals;

class TestAlbertNativeCore final : public QObject {
    Q_OBJECT

private slots:
    void parsesWindowsAndRejectsMalformedJson()
    {
        const QByteArray sample = R"([
            {
                "id": 10,
                "title": "  Current   Editor ",
                "wmClass": "Code",
                "workspace": 0,
                "onActiveWorkspace": true,
                "monitor": 0,
                "focused": true,
                "minimized": false,
                "floating": false,
                "stacked": false,
                "scratchpad": false,
                "pip": false
            },
            {
                "id": 20,
                "title": "Mozilla Firefox",
                "wmClass": "firefox",
                "workspace": 0,
                "onActiveWorkspace": true,
                "monitor": 0,
                "focused": false,
                "minimized": false,
                "floating": false,
                "stacked": true,
                "scratchpad": false,
                "pip": false
            },
            {
                "id": 30,
                "title": "Hidden Notes",
                "wmClass": "FeatherPad",
                "workspace": 2,
                "onActiveWorkspace": false,
                "monitor": 1,
                "focused": false,
                "minimized": true,
                "floating": true,
                "stacked": false,
                "scratchpad": true,
                "pip": false
            }
        ])";

        std::vector<cinnawm::WindowDescriptor> wins;
        QString err;
        QVERIFY(cinnawm::parseOpenWindowsJson(sample, wins, &err));
        QCOMPARE(wins.size(), size_t(3));
        QVERIFY(err.isEmpty());

        cinnawm::sortWindows(wins, true);
        QCOMPARE(wins[0].id, 20);
        QCOMPARE(wins[1].id, 30);
        QCOMPARE(wins[2].id, 10);

        const auto tabMeta = cinnawm::formatWindowItem(wins[0], u"firefox"_s);
        QCOMPARE(tabMeta.text, u"Mozilla Firefox"_s);
        QCOMPARE(tabMeta.actionLabel, u"Switch to Tab"_s);
        QVERIFY(tabMeta.subtext.contains(u"Tab"_s));
        QVERIFY(tabMeta.searchableSummary.contains(u"ws1"_s));

        const auto scratchMeta = cinnawm::formatWindowItem(wins[1]);
        QCOMPARE(scratchMeta.actionLabel, u"Show from Scratchpad"_s);
        QVERIFY(scratchMeta.subtext.contains(u"Scratchpad"_s));
        QVERIFY(scratchMeta.subtext.contains(u"Min"_s));

        const auto focusedMeta = cinnawm::formatWindowItem(wins[2]);
        QCOMPARE(focusedMeta.text, u"Current Editor"_s);
        QVERIFY(focusedMeta.subtext.contains(u"Focused"_s));

        QVERIFY(!cinnawm::parseOpenWindowsJson("{bad json", wins, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!cinnawm::parseOpenWindowsJson("{\"not\":\"an array\"}", wins, &err));
    }

    void parsesDesktopEntriesAndResolvesIcons()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        const QString desktopPath = tmpDir.filePath(u"org.gnome.Terminal.desktop"_s);
        QFile f(desktopPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(
            "[Desktop Entry]\n"
            "Name=Terminal\n"
            "Exec=gnome-terminal %U\n"
            "Icon=org.gnome.Terminal\n"
            "StartupWMClass=Gnome-terminal\n\n"
            "[Desktop Action new-window]\n"
            "Icon=other-action-icon\n");
        f.close();

        QString icon;
        QString startupWm;
        QVERIFY(cinnawm::parseDesktopEntryIcon(desktopPath, icon, startupWm));
        QCOMPARE(icon, u"org.gnome.Terminal"_s);
        QCOMPARE(startupWm, u"Gnome-terminal"_s);

        cinnawm::DesktopIconIndex resolver([&]() {
            return cinnawm::DesktopIconIndex::scanXdgApplications({tmpDir.path()});
        });
        QCOMPARE(resolver.resolve(u"Gnome-terminal"_s), u"org.gnome.Terminal"_s);
        QCOMPARE(resolver.resolve(u"terminal"_s), u"org.gnome.Terminal"_s);
        QCOMPARE(resolver.resolve(u"Custom App"_s), u"custom-app"_s);
        QCOMPARE(resolver.resolve(u""_s), QString());
    }

    void validatesTimeoutAndEnvOverrides()
    {
        QCOMPARE(cinnawm::parseTimeoutMs(u"abc"_s, 1500), 1500);
        QCOMPARE(cinnawm::parseTimeoutMs(u"50"_s, 1500), 1500);
        QCOMPARE(cinnawm::parseTimeoutMs(u"2500"_s, 1500), 2500);

        qputenv("CINNAWM_DBUS_NAME", "org.example.CinnaWM");
        qputenv("CINNAWM_ALBERT_TRIGGER", "win ");
        qputenv("CINNAWM_ALBERT_DBUS_NAME", "org.example.Switcher");
        qputenv("CINNAWM_ALBERT_DBUS_PATH", "/org/example/Switcher");
        const auto cfg = cinnawm::loadDbusConfigFromEnv();
        QCOMPARE(cfg.busName, u"org.example.CinnaWM"_s);
        QCOMPARE(cfg.trigger, u"win "_s);
        QCOMPARE(cfg.switcherBusName, u"org.example.Switcher"_s);
        QCOMPARE(cfg.switcherObjectPath, u"/org/example/Switcher"_s);
        qunsetenv("CINNAWM_DBUS_NAME");
        qunsetenv("CINNAWM_ALBERT_TRIGGER");
        qunsetenv("CINNAWM_ALBERT_DBUS_NAME");
        qunsetenv("CINNAWM_ALBERT_DBUS_PATH");
    }
};

QTEST_MAIN(TestAlbertNativeCore)
#include "test_albert_native.moc"
