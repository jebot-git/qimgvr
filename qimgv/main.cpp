#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QEvent>

#include "appversion.h"
#include "settings.h"
#include "components/actionmanager/actionmanager.h"
#include "utils/inputmap.h"
#include "utils/actions.h"
#include "utils/cmdoptionsrunner.h"
#include "sharedresources.h"
#include "proxystyle.h"
#include "core.h"

#ifdef __APPLE__
#include "macosapplication.h"
#endif

//------------------------------------------------------------------------------
void saveSettings() {
    delete settings;
}
//------------------------------------------------------------------------------
QDataStream& operator<<(QDataStream& out, const Script& v) {
    out << v.command << v.blocking;
    return out;
}
//------------------------------------------------------------------------------
QDataStream& operator>>(QDataStream& in, Script& v) {
    in >> v.command;
    in >> v.blocking;
    return in;
}
//------------------------------------------------------------------------------
int main(int argc, char *argv[]) {

    // force some env variables

#ifdef _WIN32
    // if this is set by other app, platform plugin may fail to load
    // https://github.com/easymodo/qimgv/issues/410
    qputenv("QT_PLUGIN_PATH","");
#endif

    // for hidpi testing
    //qputenv("QT_SCALE_FACTOR","1.5");
    //qputenv("QT_SCREEN_SCALE_FACTORS", "1;1.7");

    // do we still need this?
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR","0");

#if (QT_VERSION_MAJOR == 5)
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    // Qt6 hidpi rendering on windows still has artifacts
    // This disables it for scale factors < 1.75
    // In this case only fonts are scaled
#ifdef _WIN32
#if (QT_VERSION_MAJOR == 6)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);
#endif
#endif

    //qDebug() << qgetenv("QT_SCALE_FACTOR");
    //qDebug() << qgetenv("QT_SCREEN_SCALE_FACTORS");
    //qDebug() << qgetenv("QT_ENABLE_HIGHDPI_SCALING");

#ifdef __APPLE__
    MacOSApplication a(argc, argv);
    // default to "fusion" if available ("macos" has layout bugs, weird comboboxes etc)
    if(QStyleFactory::keys().contains("Fusion"))
        a.setStyle(QStyleFactory::create("Fusion"));
#else
    QApplication a(argc, argv);
    // use some style workarounds
    a.setStyle(new ProxyStyle);
#endif

    QCoreApplication::setOrganizationName("qimgv");
    QCoreApplication::setOrganizationDomain("github.com/jebot-git/qimgvr");
    QCoreApplication::setApplicationName("qimgvr");
    // Settings explicitly retain qimgv's existing configuration identity.
    QGuiApplication::setApplicationDisplayName("qimgvr");
    QCoreApplication::setApplicationVersion(appVersion.toString());
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
    QGuiApplication::setDesktopFileName("qimgvr");

    // needed for mpv
#ifndef _MSC_VER
    setlocale(LC_NUMERIC, "C");
#endif

#ifdef __GLIBC__
    // default value of 128k causes memory fragmentation issues
    mallopt(M_MMAP_THRESHOLD, 64000);
#endif

    // use custom types in signals
    qRegisterMetaType<ScalerRequest>("ScalerRequest");
    qRegisterMetaType<Script>("Script");
    qRegisterMetaType<std::shared_ptr<Image>>("std::shared_ptr<Image>");
    qRegisterMetaType<std::shared_ptr<Thumbnail>>("std::shared_ptr<Thumbnail>");
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qRegisterMetaTypeStreamOperators<Script>("Script");
#endif

    // globals
    inputMap = InputMap::getInstance();
    appActions = Actions::getInstance();
    settings = Settings::getInstance();
    scriptManager = ScriptManager::getInstance();
    actionManager = ActionManager::getInstance();
    shrRes = SharedResources::getInstance();

    atexit(saveSettings);

// parse args ------------------------------------------------------------------
    QCommandLineParser parser;
    QString appDescription = "qimgvr - Image viewer with automatic SBS conversion in XR.";
    appDescription.append("\nVersion: " + qApp->applicationVersion());
    appDescription.append("\nLicense: GNU GPLv3");
    parser.setApplicationDescription(appDescription);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("path", QCoreApplication::translate("main", "File or directory path."));
    parser.addOptions({
        {"gen-thumbs",
            QCoreApplication::translate("main", "Generate all thumbnails for directory."),
            QCoreApplication::translate("main", "directory-path")},
        {"gen-thumbs-size",
            QCoreApplication::translate("main", "Thumbnail size. Current size is used if not specified."),
            QCoreApplication::translate("main", "thumbnail-size")},
        {"build-options",
            QCoreApplication::translate("main", "Show build options.")},
        {"sbs", "Convert still images to SBS; display in connected WayVR or a local stereo window."},
        {"no-sbs", "Disable automatic SBS output to a connected headset."},
        {"sbs-present", "Present an existing full-SBS image on a squeezed stereo surface (internal XR viewer).", "image"},
        {"sbs-title", "Unique title for an XR stereo overlay.", "title"},
        {"sbs-present-encoded", "Internal XR presenter: base64-encoded UTF-8 image path.", "base64"},
    });
    parser.process(a);

    if(parser.isSet("sbs-present") || parser.isSet("sbs-present-encoded")) {
        // Qt Wayland appends the application display name to distinct titles.
        // The IPC overlay must keep the exact title supplied by the worker.
        if(parser.isSet("sbs-title"))
            QGuiApplication::setApplicationDisplayName(parser.value("sbs-title"));
        StereoWindow window;
        if(parser.isSet("sbs-title")) window.setWindowTitle(parser.value("sbs-title"));
        const QString image = parser.isSet("sbs-present-encoded")
            ? QString::fromUtf8(QByteArray::fromBase64(parser.value("sbs-present-encoded").toLatin1()))
            : parser.value("sbs-present");
        window.watchFile(image);
        window.showFullScreen();
        return a.exec();
    }

    if(parser.isSet("build-options")) {
        CmdOptionsRunner r;
        QTimer::singleShot(0, &r, &CmdOptionsRunner::showBuildOptions);
        return a.exec();
    } else if(parser.isSet("gen-thumbs")) {
        int size = settings->folderViewIconSize();
        if(parser.isSet("gen-thumbs-size"))
            size = parser.value("gen-thumbs-size").toInt();

        CmdOptionsRunner r;
        QTimer::singleShot(0, &r,
                           std::bind(&CmdOptionsRunner::generateThumbs, &r, parser.value("gen-thumbs"), size));
        return a.exec();
    }

// -----------------------------------------------------------------------------

    Core core;
    core.setSbsMode(parser.isSet("no-sbs") ? "off" : parser.isSet("sbs") ? "always" : "auto");

#ifdef __APPLE__
    QObject::connect(&a, &MacOSApplication::fileOpened, &core, &Core::loadPath);
#endif

    if(parser.positionalArguments().count())
        core.loadPath(parser.positionalArguments().at(0));
    else if(settings->defaultViewMode() == MODE_FOLDERVIEW)
        core.loadPath(QDir::homePath());

    // wait for event queue to catch up before showing window
    // this avoids white background flicker on windows (or not?)
    qApp->processEvents();

    core.showGui();
    return a.exec();
}
