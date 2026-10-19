#pragma once
#include <QImage>
#include <QTimer>
#include <QWidget>

// Each eye is fitted independently before squeezing into half the surface.
// WayVR must use left_right with stereo_full_frame=false for this canvas.
class StereoWindow : public QWidget {
    Q_OBJECT
public:
    explicit StereoWindow(QWidget *parent = nullptr);
    bool setImage(const QImage &image);
    void watchFile(const QString &path);
protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    QImage image;
    QTimer timer;
    QString path;
    qint64 modified = -1;
};
