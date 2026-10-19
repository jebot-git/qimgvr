#pragma once
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>
#include "gui/viewers/stereowindow.h"

class SbsController : public QObject {
    Q_OBJECT
public:
    explicit SbsController(QObject *parent = nullptr);
    ~SbsController();
    void setMode(const QString &mode);
    void setDomeIntegration(bool enabled) { domeIntegration = enabled; }
    QString inputFormat() const { return inputLayout; }
    void setInputLayout(const QString &layout) { inputLayout = layout; ++generation; if(!source.isNull() && currentMode != "off") ensureWorker(); }
    QString mode() const { return currentMode; }
    bool headsetConnected() const { return connected; }
    void setSource(const QImage &image, const QString &fileName = QString());
signals:
    void message(const QString &text);
    void failed(const QString &text);
    void converted(quint64 generation, const QString &file);
private:
    QProcess worker;
    QTemporaryDir temporary;
    QTimer poll;
    std::unique_ptr<StereoWindow> preview;
    QImage source;
    QString sourceName;
    QString inputLayout = "auto";
    bool domeIntegration = false;
    QByteArray buffer;
    QString currentMode = "auto";
    QString currentOutput;
    quint64 generation = 0, completed = 0;
    bool busy = false, connected = false, hidePending = false, stopping = false;
    void ensureWorker();
    void dispatch();
    void send(const QJsonObject &request);
    void readResponse();
    void response(const QJsonObject &result);
    void showPreview();
};
