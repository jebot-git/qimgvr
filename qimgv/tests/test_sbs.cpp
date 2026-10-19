#include <QtTest>
#include <QPainter>
#include <QTemporaryDir>
#include "components/sbscontroller.h"
#include "gui/viewers/stereowindow.h"

class TestSbs : public QObject {
    Q_OBJECT
private slots:
    void presenterReloadsAtomicFilesAndClosesWithOwner() {
        QTemporaryDir directory;
        const QString path = directory.filePath("display.png");
        QImage frame(80, 40, QImage::Format_RGB32);
        frame.fill(Qt::red);
        QVERIFY(frame.save(path));
        StereoWindow window;
        window.resize(400, 200);
        window.watchFile(path);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(window.grab().toImage().pixelColor(100, 100), QColor(Qt::red), 1000);
        QTest::qWait(20);
        frame.fill(Qt::blue);
        const QString next = directory.filePath("next.png");
        QVERIFY(frame.save(next));
        QVERIFY(QFile::remove(path));
        QVERIFY(QFile::rename(next, path));
        QTRY_COMPARE_WITH_TIMEOUT(window.grab().toImage().pixelColor(100, 100), QColor(Qt::blue), 1000);
        QVERIFY(directory.remove());
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 1000);
    }

    void canvasSeparatesEyesAndCentersPortraits() {
        StereoWindow window;
        window.resize(400, 200);
        QImage image(80, 400, QImage::Format_RGB32);
        image.fill(Qt::red);
        { QPainter p(&image); p.fillRect(40, 0, 40, 400, Qt::blue); }
        QVERIFY(window.setImage(image));
        window.show();
        QTest::qWait(20);
        QImage capture = window.grab().toImage();
        QCOMPARE(capture.pixelColor(100, 100), QColor(Qt::red));
        QCOMPARE(capture.pixelColor(300, 100), QColor(Qt::blue));
        QCOMPARE(capture.pixelColor(199, 100), QColor(Qt::black));
        QCOMPARE(capture.pixelColor(200, 100), QColor(Qt::black));
        QCOMPARE(capture.pixelColor(50, 100), QColor(Qt::black));
        QCOMPARE(capture.pixelColor(250, 100), QColor(Qt::black));
        QVERIFY(!window.setImage(QImage(81, 40, QImage::Format_RGB32)));
    }

    void coalescesNavigationAndSuppressesDisabledResults() {
        QTemporaryDir directory;
        const QString script = directory.filePath("fake-python");
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/usr/bin/python3\nimport sys,json,time,shutil\n"
                   "for line in sys.stdin:\n"
                   " r=json.loads(line); action=r.get('action')\n"
                   " if action=='quit': break\n"
                   " if action=='convert':\n"
                   "  time.sleep(.25); shutil.copyfile(r['input'],r['output'])\n"
                   " print(json.dumps(dict(r,status='ok',connected=False)),flush=True)\n");
        file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("QIMGV_SBS_PYTHON", script.toUtf8());
        {
            SbsController controller;
            QSignalSpy ready(&controller, &SbsController::converted);
            QSignalSpy messages(&controller, &SbsController::message);
            QSignalSpy errors(&controller, &SbsController::failed);
            controller.setMode("always");
            QImage image(60, 40, QImage::Format_RGB32);
            image.fill(Qt::red);
            controller.setSource(image);
            QTRY_VERIFY_WITH_TIMEOUT(messages.count() > 0, 2000);
            image.fill(Qt::blue);
            controller.setSource(image);
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 3000);
            const QString output = ready.first()[1].toString();
            QCOMPARE(QImage(output).pixelColor(10, 10), QColor(Qt::blue));
            QCOMPARE(errors.count(), 0);
            image.fill(Qt::green);
            controller.setSource(image);
            controller.setMode("off");
            QTest::qWait(700);
            QCOMPARE(ready.count(), 1);
        }
        qunsetenv("QIMGV_SBS_PYTHON");
    }
};

QTEST_MAIN(TestSbs)
#include "test_sbs.moc"
