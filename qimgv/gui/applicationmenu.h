#pragma once

#include <QMap>
#include <QMenuBar>

// A native menu model for DBusMenu hosts, including DomeD. The custom context
// menu remains available on desktops without a global menu host.
class ApplicationMenu : public QMenuBar {
    Q_OBJECT
public:
    explicit ApplicationMenu(QWidget *parent = nullptr);
    void updateState(bool hasFile, bool hasImage, bool edited, bool hasDirectory,
                     bool slideshow, bool shuffle, bool fullscreen);
    void updateShortcuts(const QMap<QString, QString> &shortcuts);

signals:
    void actionRequested(const QString &name);

private:
    enum Scope { Always, File, Image, Edited, Directory };
    QAction *addCommand(QMenu *menu, const QString &text, const QString &name,
                        Scope scope = Always, bool checkable = false);
    QMap<QString, QAction *> commands;
    QMap<QAction *, Scope> scopes;
};
