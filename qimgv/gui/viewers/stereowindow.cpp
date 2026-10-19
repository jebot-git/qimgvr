#include "stereowindow.h"
#include <QFileInfo>
#include <QDateTime>
#include <QKeyEvent>
#include <QPainter>

StereoWindow::StereoWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle(tr("qimgv — SBS image"));
    resize(1280, 720);
    setMinimumSize(200, 100);
    timer.setInterval(150);
    connect(&timer, &QTimer::timeout, this, [this] {
        QFileInfo file(path);
        // Atomic rename changes metadata even when the path stays the same.
        const qint64 stamp = file.lastModified().toMSecsSinceEpoch();
        if(file.exists() && stamp != modified) {
            QImage next(path);
            if(setImage(next)) modified = stamp;
        }
        // The owner removes this directory on exit. Do not leave orphan viewers.
        if(!QFileInfo(file.absolutePath()).exists()) close();
    });
}

bool StereoWindow::setImage(const QImage &next) {
    if(next.isNull() || next.width() % 2) return false;
    image = next;
    update();
    return true;
}

void StereoWindow::watchFile(const QString &file) {
    path = file;
    modified = -1;
    timer.start();
    QMetaObject::invokeMethod(&timer, "timeout", Qt::QueuedConnection);
}

void StereoWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if(image.isNull()) return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const int eyeWidth = image.width() / 2;
    const QSizeF fitted = QSizeF(eyeWidth, image.height()).scaled(QSizeF(width(), height()), Qt::KeepAspectRatio);
    for(int eye = 0; eye < 2; ++eye) {
        const QRectF source(eye * eyeWidth, 0, eyeWidth, image.height());
        const QRectF target(eye * width() / 2.0 + (width() - fitted.width()) / 4.0,
                            (height() - fitted.height()) / 2.0, fitted.width() / 2.0, fitted.height());
        painter.drawImage(target, image, source);
    }
}

void StereoWindow::keyPressEvent(QKeyEvent *event) {
    if(event->key() == Qt::Key_Escape) close();
    else if(event->key() == Qt::Key_F11) isFullScreen() ? showNormal() : showFullScreen();
    else QWidget::keyPressEvent(event);
}
