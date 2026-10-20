#include "applicationmenu.h"

#include <QAction>
#include <QMenu>

ApplicationMenu::ApplicationMenu(QWidget *parent) : QMenuBar(parent) {
    setNativeMenuBar(true);
    // Do not add a local bar to qimgv's borderless image viewer. Keeping the
    // widget alive lets Qt export and associate its model with the parent window.
    setFixedHeight(0);

    auto *file = addMenu(tr("&File"));
    addCommand(file, tr("&Open…"), "open");
    addCommand(file, tr("&Save"), "save", Edited);
    addCommand(file, tr("Save &As…"), "saveAs", Image);
    file->addSeparator();
    addCommand(file, tr("&Print…"), "print", Image);
    addCommand(file, tr("Show in Folder"), "showInDirectory", File);
    addCommand(file, tr("&Rename…"), "renameFile", File);
    addCommand(file, tr("Move to Trash"), "moveToTrash", File);
    file->addSeparator();
    addCommand(file, tr("&Quit"), "exit")->setMenuRole(QAction::QuitRole);

    auto *edit = addMenu(tr("&Edit"));
    addCommand(edit, tr("Discard Edits"), "discardEdits", Edited);
    edit->addSeparator();
    addCommand(edit, tr("&Copy File"), "copyFileClipboard", File);
    addCommand(edit, tr("Copy File Path"), "copyPathClipboard", File);
    addCommand(edit, tr("&Paste File"), "pasteFile");
    addCommand(edit, tr("Quick Copy…"), "copyFile", File);
    addCommand(edit, tr("Quick Move…"), "moveFile", File);
    edit->addSeparator();
    addCommand(edit, tr("Rotate Left"), "rotateLeft", Image);
    addCommand(edit, tr("Rotate Right"), "rotateRight", Image);
    addCommand(edit, tr("Flip Horizontally"), "flipH", Image);
    addCommand(edit, tr("Flip Vertically"), "flipV", Image);
    addCommand(edit, tr("&Crop…"), "crop", Image);
    addCommand(edit, tr("&Resize…"), "resize", Image);
    edit->addSeparator();
    addCommand(edit, tr("&Settings…"), "openSettings")->setMenuRole(QAction::PreferencesRole);

    auto *view = addMenu(tr("&View"));
    addCommand(view, tr("Zoom In"), "zoomIn", Image);
    addCommand(view, tr("Zoom Out"), "zoomOut", Image);
    addCommand(view, tr("Fit Window"), "fitWindow", Image);
    addCommand(view, tr("Fit Width"), "fitWidth", Image);
    addCommand(view, tr("Original Size"), "fitNormal", Image);
    addCommand(view, tr("Stretch to Window"), "fitWindowStretch", Image);
    view->addSeparator();
    addCommand(view, tr("Folder View"), "folderView");
    addCommand(view, tr("Image Information"), "toggleImageInfo", File);
    addCommand(view, tr("Reload Image"), "reloadImage", File);
    addCommand(view, tr("Toggle SBS / XR Output"), "toggleSbs", Image);
    addCommand(view, tr("Cycle Input Layout"), "cycleInputLayout", Image);

    auto *go = addMenu(tr("&Go"));
    addCommand(go, tr("Previous Image"), "prevImage", Directory);
    addCommand(go, tr("Next Image"), "nextImage", Directory);
    addCommand(go, tr("First Image"), "jumpToFirst", Directory);
    addCommand(go, tr("Last Image"), "jumpToLast", Directory);
    go->addSeparator();
    addCommand(go, tr("Slideshow"), "toggleSlideshow", Directory, true);
    addCommand(go, tr("Shuffle"), "toggleShuffle", Directory, true);

    auto *window = addMenu(tr("&Window"));
    addCommand(window, tr("Fullscreen"), "toggleFullscreen", Always, true);

    updateState(false, false, false, false, false, false, false);
}

QAction *ApplicationMenu::addCommand(QMenu *menu, const QString &text,
                                    const QString &name, Scope scope, bool checkable) {
    auto *action = menu->addAction(text);
    action->setObjectName(name);
    action->setMenuRole(QAction::NoRole);
    // Publish accelerators to the native host without competing with
    // ActionManager or text fields for keyboard events inside the client.
    action->setShortcutContext(Qt::WidgetShortcut);
    action->setCheckable(checkable);
    commands.insert(name, action);
    scopes.insert(action, scope);
    connect(action, &QAction::triggered, this, [this, name] { emit actionRequested(name); });
    return action;
}

void ApplicationMenu::updateState(bool hasFile, bool hasImage, bool edited,
                                  bool hasDirectory, bool slideshow, bool shuffle,
                                  bool fullscreen) {
    for(auto i = scopes.cbegin(); i != scopes.cend(); ++i) {
        bool enabled = true;
        switch(i.value()) {
        case Always: break;
        case File: enabled = hasFile; break;
        case Image: enabled = hasImage; break;
        case Edited: enabled = hasImage && edited; break;
        case Directory: enabled = hasDirectory; break;
        }
        i.key()->setEnabled(enabled);
    }
    commands.value("toggleSlideshow")->setChecked(slideshow);
    commands.value("toggleShuffle")->setChecked(shuffle);
    commands.value("toggleFullscreen")->setChecked(fullscreen);
}

void ApplicationMenu::updateShortcuts(const QMap<QString, QString> &shortcuts) {
    for(auto i = commands.cbegin(); i != commands.cend(); ++i) {
        QList<QKeySequence> keys;
        for(auto binding = shortcuts.cbegin(); binding != shortcuts.cend(); ++binding) {
            if(binding.value() != i.key())
                continue;
            // Mouse bindings belong to ActionManager, not Qt's shortcut parser.
            if(binding.key().contains("Wheel") || binding.key().contains("LMB") ||
                    binding.key().contains("RMB") || binding.key().contains("MMB"))
                continue;
            const auto key = QKeySequence::fromString(binding.key(), QKeySequence::PortableText);
            if(!key.isEmpty() && key[0] != QKeySequence(Qt::Key_unknown)[0])
                keys.append(key);
        }
        i.value()->setShortcuts(keys);
    }
}
