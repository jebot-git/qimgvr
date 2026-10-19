#include <QtTest>
#include <QLibrary>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>
#include "gui/viewers/videoplayer.h"

class TestPlayer : public QObject {
    Q_OBJECT
private slots:
    void pluginLoadsAndRendersVideo() {
        QTemporaryDir directory;
        const QString video = directory.filePath("test.mp4");
        QCOMPARE(QProcess::execute("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
            "testsrc2=size=320x180:rate=24", "-t", "3", "-c:v", "libx264", video}), 0);
        QLibrary library(QString::fromUtf8(PLAYER_PLUGIN));
        auto factory = reinterpret_cast<VideoPlayer*(*)()>(library.resolve("CreatePlayerWidget"));
        QVERIFY2(factory, qPrintable(library.errorString()));
        std::unique_ptr<VideoPlayer> player(factory());
        QVERIFY(player);
        // This briefly visible integration test must not pause on stray clicks
        // or steal focus while running in the user's live desktop session.
        player->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        player->setWindowFlag(Qt::WindowTransparentForInput);
        player->resize(640, 360);
        player->setMuted(true);
        QSignalSpy position(player.get(), &VideoPlayer::positionChanged);
        player->show();
        QVERIFY(QTest::qWaitForWindowExposed(player.get()));
        QVERIFY(player->showVideo(video));
        QTRY_VERIFY_WITH_TIMEOUT(!position.isEmpty() && position.last()[0].toInt() >= 1, 10000);
        const QImage frame = player->grab().toImage();
        QVERIFY(!frame.isNull());
        bool hasColor = false;
        for(int y = 0; y < frame.height(); y += 10)
            for(int x = 0; x < frame.width(); x += 10) {
                const QColor color = frame.pixelColor(x, y);
                if(color.red() > color.blue() + 40 || color.blue() > color.red() + 40) hasColor = true;
            }
        QVERIFY2(hasColor, "Decoded video must produce colored pixels in the OpenGL widget");
        player->stop();
        player.reset();
    }
};

QTEST_MAIN(TestPlayer)
#include "test_player.moc"
