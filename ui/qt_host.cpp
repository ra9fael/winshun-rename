// qt_host.cpp — the Qt Quick half of the window.
//
// Threading follows the workspace's shared-Qt convention: this file is only ever
// touched on the GUI thread, and the protocol thread reaches it through the
// functions in qt_host.h, each of which marshals with QMetaObject::invokeMethod.
#include "ui/qt_host.h"

#include "ui/bridge.h"
#include "win/platform.h"
#include "winshun.h"

#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <cstdio>
#include <memory>

namespace rename_host {

namespace {

// Owned by the GUI thread. The engine outlives the scene because it keeps the
// compiled QML and the theme object alive; both are parented to objects freed
// when Run() returns.
struct Ui
{
    QGuiApplication* app = nullptr;
    QQmlEngine* engine = nullptr;
    rename_qml::Bridge* bridge = nullptr;
    QObject* theme = nullptr;
    QQuickWindow* window = nullptr;

    // The selection that arrived before the window existed, so a fast host that
    // sends `files` before the scene is up still gets its rows.
    bool hasPendingSelection = false;
    std::vector<std::wstring> pendingPaths;
    bool pendingRecursive = false;
};

std::unique_ptr<Ui>& Instance()
{
    static std::unique_ptr<Ui> ui;
    return ui;
}

QString ToQt(const std::wstring& text) { return QString::fromStdWString(text); }

// `nativeLook` as the host sent it, into the QVariantMap QML reads. Only scalars
// are taken: the contract says colours are "#AARRGGBB" strings and radii are
// numbers, and an unknown key is ignored by QML anyway (docs/plugins.md).
QVariantMap ParseLook(const std::string& json)
{
    QVariantMap map;
    if (json.empty())
        return map;

    const auto parsed = winshun::Json::parse(json);
    if (!parsed || !parsed->isObject())
        return map;

    for (const auto& [key, value] : parsed->toObject())
    {
        if (value.isString())
            map.insert(QString::fromStdString(key), QString::fromStdString(value.toString()));
        else if (value.isBool())
            map.insert(QString::fromStdString(key), value.toBool(false));
        else if (value.isNumber())
            map.insert(QString::fromStdString(key), value.toNumber());
    }
    return map;
}

void ShowNow(const std::vector<std::wstring>& paths, bool recursive)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->window || !ui->bridge)
        return;

    QStringList selection;
    selection.reserve(static_cast<qsizetype>(paths.size()));
    for (const std::wstring& path : paths)
        selection.push_back(ToQt(path));
    ui->bridge->setSelection(selection, recursive);

    ui->window->show();
    ui->window->raise();
    ui->window->requestActivate();

    // RENAME_GRAB_WINDOW=<path>: render the finished window to a PNG and exit.
    // A diagnostic hook, not a feature: it is the only way to check the look on
    // a machine whose screen cannot be captured (a disconnected session, CI),
    // and it goes through the same render path the user sees because
    // QQuickWindow::grabWindow() asks the scene graph for its content.
    // RENAME_GRAB_RULE=<find>\n<replace>: applied as the single rule first, so
    // the grab shows a real preview rather than an untouched table.
    // RENAME_GRAB_MODE=text: switch to the oil buffer first.
    // RENAME_GRAB_APPLY=1: run the batch before grabbing, so a grab proves the
    // whole chain (preview -> execute -> refreshed table) on a real directory.
    const QString grabTo = qEnvironmentVariable("RENAME_GRAB_WINDOW");
    if (grabTo.isEmpty())
        return;
    if (qEnvironmentVariable("RENAME_GRAB_MODE") == QLatin1String("text"))
        ui->bridge->switchMode(QStringLiteral("text"));
    const QStringList rule = qEnvironmentVariable("RENAME_GRAB_RULE").split(QLatin1Char('\n'));
    if (rule.size() == 2)
        ui->bridge->setSingleRule(0, 0, rule[0], rule[1], true);
    if (qEnvironmentVariable("RENAME_GRAB_APPLY") == QLatin1String("1"))
        ui->bridge->apply();
    QTimer::singleShot(1200, ui->window, [window = ui->window, grabTo] {
        // grabWindow() must run on the GUI thread, which is where this fires.
        const QImage image = window->grabWindow();
        const bool saved = image.save(grabTo, "PNG");
        std::fprintf(stderr, "[rename] grab %s: %s (%dx%d)\n",
                     saved ? "written" : "FAILED",
                     grabTo.toUtf8().constData(), image.width(), image.height());
        std::fflush(stderr);
        QCoreApplication::quit();
    });
}

void ApplyLookNow(const std::string& locale, bool dark, const std::string& uiSize,
                  const std::string& nativeLookJson)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->bridge)
        return;
    ui->bridge->ApplyAppearance(QString::fromStdString(locale), dark,
                                QString::fromStdString(uiSize), ParseLook(nativeLookJson));
}

} // namespace

bool Start(int argc, char** argv, const std::wstring& dataDir, std::string& error)
{
    auto ui = std::make_unique<Ui>();

    ui->app = new QGuiApplication(argc, argv);
    ui->app->setApplicationName(QStringLiteral("Rename"));
    // The host's own behaviour: closing the window must not end the process, and
    // the protocol thread decides when it does.
    ui->app->setQuitOnLastWindowClosed(false);

    // QtQuick.Controls.Basic is the style the shared-Qt contract guarantees
    // (docs/plugins.md); Main.qml styles every control itself, so Basic is all
    // that is needed and no other style has to ship in the package.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // The resident plugin stays headless-capable: the normalizer is what
    // finalize() compares names with, and the window path needs it too.
    rename_win::InstallNfcNormalizer();

    ui->bridge = new rename_qml::Bridge();
    if (!dataDir.empty())
        ui->bridge->SetDataDir(dataDir);

    // Parented to the bridge so the engine dies with it, after app.exec().
    ui->engine = new QQmlEngine(ui->bridge);

    // The theme is created once from Tokens.qml and handed to the scene as the
    // context property `Theme`, so every component reads the same tokens and the
    // whole palette can be replaced in one place when the host's look changes.
    // (The file is not named Theme.qml: it and Main.qml share a directory, which
    // QML imports implicitly, so `Theme.qml` would become a type shadowing the
    // property.)
    QQmlComponent themeComponent(ui->engine,
                                 QUrl(QStringLiteral("qrc:/qt/qml/Rename/Tokens.qml")));
    if (themeComponent.isError())
    {
        error = themeComponent.errorString().toStdString();
        delete ui->bridge;
        return false;
    }
    ui->theme = themeComponent.create();
    if (!ui->theme)
    {
        error = themeComponent.errorString().toStdString();
        delete ui->bridge;
        return false;
    }
    ui->theme->setParent(ui->bridge);

    auto* context = new QQmlContext(ui->engine);
    context->setContextProperty(QStringLiteral("bridge"), ui->bridge);
    context->setContextProperty(QStringLiteral("Theme"), ui->theme);

    QQmlComponent component(ui->engine, QUrl(QStringLiteral("qrc:/qt/qml/Rename/Main.qml")));
    if (component.isError())
    {
        error = component.errorString().toStdString();
        delete ui->bridge;
        return false;
    }
    QObject* object = component.create(context);
    if (!object)
    {
        error = component.errorString().toStdString();
        delete ui->bridge;
        return false;
    }
    // The context goes with the scene: deleting the window frees both.
    context->setParent(object);

    ui->window = qobject_cast<QQuickWindow*>(object);
    if (!ui->window)
    {
        error = "the root QML object is not a Window";
        delete object;
        delete ui->bridge;
        return false;
    }

    // `look` is a plain property on the theme object, so the same assignment path
    // serves the first paint and every later change.
    QObject::connect(ui->bridge, &rename_qml::Bridge::appearanceChanged, ui->theme,
                     [theme = ui->theme, bridge = ui->bridge] {
                         theme->setProperty("look", bridge->look());
                         theme->setProperty("uiSize", bridge->uiSize());
                     });

    Instance() = std::move(ui);
    return true;
}

void Show(const std::vector<std::wstring>& paths, bool recursive)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->bridge)
        return;

    if (!ui->window)
    {
        // Queued on the GUI thread: Start() has run by the time the event loop
        // turns, so the scene is there. Until then the selection is kept.
        ui->hasPendingSelection = true;
        ui->pendingPaths = paths;
        ui->pendingRecursive = recursive;
        return;
    }
    QMetaObject::invokeMethod(ui->bridge,
                              [paths, recursive] { ShowNow(paths, recursive); },
                              Qt::QueuedConnection);
}

void ApplyLook(const std::string& locale, bool dark, const std::string& uiSize,
               const std::string& nativeLookJson)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->bridge)
        return;
    QMetaObject::invokeMethod(
        ui->bridge,
        [locale, dark, uiSize, nativeLookJson] {
            ApplyLookNow(locale, dark, uiSize, nativeLookJson);
        },
        Qt::QueuedConnection);
}

void SetDataDir(const std::wstring& dataDir)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->bridge)
        return;
    QMetaObject::invokeMethod(
        ui->bridge, [dataDir] {
            if (Ui* current = Instance().get())
                if (current->bridge)
                    current->bridge->SetDataDir(dataDir);
        },
        Qt::QueuedConnection);
}

int Run()
{
    Ui* ui = Instance().get();
    if (!ui || !ui->app)
        return 1;

    const int rc = ui->app->exec();

    // Tear down on the GUI thread, in dependency order: the scene keeps its
    // context, the engine keeps the theme, and the bridge owns the engine.
    if (ui->window)
    {
        delete ui->window;
        ui->window = nullptr;
    }
    if (ui->bridge)
    {
        delete ui->bridge;   // deletes the engine and the theme with it
        ui->bridge = nullptr;
        ui->theme = nullptr;
        ui->engine = nullptr;
    }
    Instance().reset();
    return rc;
}

void Quit()
{
    if (QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection);
}

} // namespace rename_host
