#include <QtTest>
#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include "gui/applicationmenu.h"

class TestApplicationMenu : public QObject {
    Q_OBJECT
private slots:
    void dispatchAndAvailability() {
        ApplicationMenu menu;
        QSignalSpy requested(&menu, &ApplicationMenu::actionRequested);
        auto action = [&menu](const char *name) { return menu.findChild<QAction *>(name); };
        QVERIFY(action("open")->isEnabled());
        QVERIFY(!action("rotateLeft")->isEnabled());
        QVERIFY(!action("copyFileClipboard")->isEnabled());
        QVERIFY(!action("save")->isEnabled());
        QVERIFY(!action("nextImage")->isEnabled());
        menu.updateState(true, false, false, true, false, false, false);
        QVERIFY(action("copyFileClipboard")->isEnabled());
        QVERIFY(action("nextImage")->isEnabled());
        QVERIFY(!action("rotateLeft")->isEnabled());
        menu.updateState(true, true, true, true, true, true, true);
        QVERIFY(action("rotateLeft")->isEnabled());
        QVERIFY(action("save")->isEnabled());
        QVERIFY(action("toggleSlideshow")->isChecked());
        QVERIFY(action("toggleShuffle")->isChecked());
        QVERIFY(action("toggleFullscreen")->isChecked());
        action("rotateLeft")->trigger();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.takeFirst().at(0).toString(), QString("rotateLeft"));
        action("toggleSbs")->trigger();
        QCOMPARE(requested.takeFirst().at(0).toString(), QString("toggleSbs"));
        menu.updateState(false, false, false, false, false, false, false);
        QVERIFY(!action("rotateLeft")->isEnabled());
        QVERIFY(!action("save")->isEnabled());
        QVERIFY(!action("toggleSlideshow")->isChecked());
        QVERIFY(!action("toggleFullscreen")->isChecked());
    }

    void shortcutChangesAndTextInput() {
        QWidget window;
        ApplicationMenu menu(&window);
        QLineEdit input(&window);
        auto *copy = menu.findChild<QAction *>("copyFileClipboard");
        menu.updateState(true, true, false, true, false, false, false);
        menu.updateShortcuts({{"Ctrl+C", "copyFileClipboard"}, {"Ctrl+WheelUp", "zoomIn"}});
        QCOMPARE(copy->shortcut(), QKeySequence("Ctrl+C"));
        QVERIFY(menu.findChild<QAction *>("zoomIn")->shortcut().isEmpty());
        QSignalSpy requested(&menu, &ApplicationMenu::actionRequested);
        window.show();
        input.setFocus();
        input.setText("selected text");
        input.selectAll();
        QTest::keyClick(&input, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(requested.count(), 0);
        menu.updateShortcuts({{"Ctrl+Shift+C", "copyFileClipboard"}});
        QCOMPARE(copy->shortcut(), QKeySequence("Ctrl+Shift+C"));
        menu.updateShortcuts({});
        QVERIFY(copy->shortcuts().isEmpty());
    }
};

QTEST_MAIN(TestApplicationMenu)
#include "test_applicationmenu.moc"
