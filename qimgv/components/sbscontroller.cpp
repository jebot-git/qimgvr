#include "sbscontroller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

SbsController::SbsController(QObject *parent) : QObject(parent) {
    poll.setInterval(3000);
    connect(&poll, &QTimer::timeout, this, &SbsController::dispatch);
    connect(&worker, &QProcess::started, this, &SbsController::dispatch);
    connect(&worker, &QProcess::readyReadStandardOutput, this, &SbsController::readResponse);
    connect(&worker, &QProcess::readyReadStandardError, this, [this] {
        qWarning().noquote() << "SBS worker:" << worker.readAllStandardError().trimmed();
    });
    connect(&worker, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if(!stopping) emit failed(tr("SBS worker: ") + worker.errorString());
    });
    connect(&worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int, QProcess::ExitStatus) {
        busy = false;
        poll.stop();
        if(!stopping) emit failed(tr("SBS converter exited. Check the installed Arc SBS runtime; toggle SBS to retry."));
    });
}

SbsController::~SbsController() {
    stopping = true;
    poll.stop();
    if(worker.state() != QProcess::NotRunning) {
        worker.write("{\"action\":\"quit\"}\n");
        worker.closeWriteChannel();
        if(!worker.waitForFinished(2000)) {
            worker.terminate();
            if(!worker.waitForFinished(1000)) { worker.kill(); worker.waitForFinished(1000); }
        }
    }
}

void SbsController::setMode(const QString &mode) {
    currentMode = mode;
    ++generation;
    hidePending = true;
    if(preview) preview->hide();
    if(mode == "off") { poll.stop(); if(!busy) dispatch(); }
    else if(!source.isNull()) ensureWorker();
}

void SbsController::setSource(const QImage &image, const QString &fileName) {
    source = image;
    sourceName = fileName;
    ++generation;
    if(source.isNull()) {
        hidePending = true;
        if(preview) preview->hide();
        if(!busy) dispatch();
    } else if(currentMode != "off") ensureWorker();
}

void SbsController::ensureWorker() {
    if(!temporary.isValid()) { emit failed(tr("Cannot create SBS working directory")); return; }
    if(worker.state() == QProcess::NotRunning) {
        QString python = qEnvironmentVariable("QIMGV_SBS_PYTHON");
        if(python.isEmpty() && QFile::exists("/opt/arc-sbs/venv/bin/python")) python = "/opt/arc-sbs/venv/bin/python";
        if(python.isEmpty()) python = QDir::homePath() + "/.local/opt/arc-sbs/current/venv/bin/python";
        if(!QFile::exists(python)) python = QStandardPaths::findExecutable("python3");
        buffer.clear();
        busy = false;
        QStringList arguments{"-u", "-m", "arc_sbs.image_worker"};
        if(domeIntegration) arguments << "--xr-backend" << "domed";
        worker.start(python, arguments);
    } else dispatch();
    poll.start();
}

void SbsController::send(const QJsonObject &request) {
    busy = true;
    worker.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
}

void SbsController::dispatch() {
    if(stopping || busy || worker.state() != QProcess::Running) return;
    if(hidePending) {
        hidePending = false;
        send({{"action", "hide"}});
    } else if(currentMode != "off" && !source.isNull() && completed != generation) {
        // Snapshot qimgv's already oriented/edited image, not the file on disk.
        QImage scaled = source;
        if(source.width() > 3840 || source.height() > 3840)
            scaled = source.scaled(3840, 3840, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        // PNG stores straight alpha; retain the viewer's oriented/edited pixels.
        QImage snapshot = scaled.convertToFormat(scaled.hasAlphaChannel()
            ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
        const QString input = temporary.filePath("input.png");
        if(!snapshot.save(input, "PNG")) {
            completed = generation;
            emit failed(tr("Cannot save image for SBS conversion"));
            return;
        }
        emit message(tr("Preparing SBS image…"));
        QJsonObject metadata;
        for(const auto &key : source.textKeys()) metadata.insert(key, source.text(key));
        send({{"action", "convert"}, {"id", qint64(generation)}, {"mode", currentMode},
              {"source_name", sourceName}, {"metadata", metadata}, {"input_layout", inputLayout},
              {"input", input}, {"output", temporary.filePath(QString::number(generation) + ".png")}});
    } else if(currentMode != "off" && !source.isNull()) send({{"action", "probe"}});
}

void SbsController::readResponse() {
    buffer += worker.readAllStandardOutput();
    int newline;
    while((newline = buffer.indexOf('\n')) >= 0) {
        const auto line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        QJsonParseError error;
        auto document = QJsonDocument::fromJson(line, &error);
        if(error.error != QJsonParseError::NoError || !document.isObject()) {
            busy = false;
            emit failed(tr("Invalid response from SBS converter"));
            continue;
        }
        busy = false;
        response(document.object());
    }
}

void SbsController::showPreview() {
    if(!preview) preview = std::make_unique<StereoWindow>();
    if(preview->setImage(QImage(currentOutput))) preview->show();
}

void SbsController::response(const QJsonObject &result) {
    const QString action = result["action"].toString();
    const quint64 id = result["id"].toVariant().toULongLong();
    const bool wasConnected = connected;
    if(result.contains("connected")) connected = result["connected"].toBool();
    if(action == "convert" && id != generation) {
        QFile::remove(temporary.filePath(QString::number(id) + ".png"));
        dispatch(); // latest image wins; obsolete results never get presented
        return;
    }
    if(result["status"] == "error") {
        completed = generation;
        emit failed(tr("SBS: ") + result["error"].toString());
        if(action == "present" && currentMode == "always") showPreview();
        return;
    }
    if(action == "convert") {
        completed = generation;
        if(result["status"] == "skipped") return;
        if(!currentOutput.isEmpty()) QFile::remove(currentOutput);
        currentOutput = result["output"].toString();
        if(result["depth_bypassed"].toBool()) emit message(tr("Existing SBS image — depth conversion skipped"));
        qInfo().noquote() << "SBS image ready:" << result["device"].toString()
                          << "seconds:" << result["seconds"].toDouble();
        emit converted(generation, currentOutput);
        if(connected) {
            if(preview) preview->hide();
            send({{"action", "present"}, {"id", qint64(generation)}, {"output", currentOutput},
                  {"display", temporary.filePath("display.png")},
                  {"presenter", QCoreApplication::applicationFilePath()}});
        } else if(currentMode == "always") showPreview();
    } else if(action == "probe") {
        if(connected && !wasConnected) { ++generation; dispatch(); }
        else if(!connected && wasConnected && currentMode == "always" && !currentOutput.isEmpty()) showPreview();
    } else if(action == "present") {
        qInfo().noquote() << "SBS XR output:" << result["overlay"].toString() << "connected:" << connected;
        emit message(connected ? tr("SBS image displayed in XR") : tr("Headset disconnected"));
        if(!connected && currentMode == "always") showPreview();
    } else if(action == "hide") dispatch();
    if(hidePending || (currentMode != "off" && completed != generation)) dispatch();
}
