// qt_host.cpp — the Qt Widgets window host.
//
// The resident plugin and its window are one process. Qt Widgets needs the GUI
// thread, so the Qt event loop owns the main thread and the JSON-RPC loop moves
// to a worker (main.cpp). The protocol thread reaches this file through the
// functions in qt_host.h; each marshals to the GUI thread.
//
// The window is built lazily on the first Show(), so an installed plugin that is
// never opened pays for one QApplication and nothing else.
#include "ui/qt_host.h"

#include "ui/bridge.h"
#include "ui/widgets_window.h"
#include "win/platform.h"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaObject>
#include <QPixmap>
#include <QStyleFactory>
#include <QString>
#include <QTableView>
#include <QTimer>
#include <QVariantMap>

#include <cstdio>
#include <memory>

namespace rename_host {

namespace {

struct Ui
{
    QApplication* app = nullptr;
    bool ownsApp = false;
    rename_qml::Bridge* bridge = nullptr;
    rename_widgets::Window* window = nullptr;
};

std::unique_ptr<Ui>& Instance()
{
    static std::unique_ptr<Ui> ui;
    return ui;
}

QString ToQt(const std::wstring& text) { return QString::fromStdWString(text); }

// `nativeLook` as the host sent it. Only scalars are taken: the contract says
// colours are "#AARRGGBB" strings and radii are numbers; an unknown key is
// ignored by the Widgets theme. Parsed with Qt's own reader so this layer has no
// dependency on winshun.h (the standalone wrapper links the same code).
QVariantMap ParseLook(const std::string& json)
{
    QVariantMap map;
    if (json.empty())
        return map;

    QJsonParseError problem{};
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &problem);
    if (problem.error != QJsonParseError::NoError || !document.isObject())
        return map;

    const QJsonObject object = document.object();
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        switch (it.value().type())
        {
        case QJsonValue::String:
            map.insert(it.key(), it.value().toString());
            break;
        case QJsonValue::Bool:
            map.insert(it.key(), it.value().toBool());
            break;
        case QJsonValue::Double:
            map.insert(it.key(), it.value().toDouble());
            break;
        default:
            break;   // an object or array token is not a token; ignore it
        }
    }
    return map;
}

bool BuildWindow(std::string& error)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->bridge)
    {
        error = "the bridge is not ready";
        return false;
    }
    if (ui->window)
        return true;

    ui->window = new rename_widgets::Window(ui->bridge);
    if (!ui->window)
    {
        error = "the widgets window could not be created";
        return false;
    }
    return true;
}

// RENAME_GRAB_WINDOW=<path>: render the finished window to a PNG and exit.
// RENAME_GRAB_RULE=<find>\n<replace> and RENAME_GRAB_MODE=text preset the view;
// RENAME_GRAB_APPLY=1 runs the batch first, so one grab proves the whole chain.
void GrabIfAsked(Ui* ui)
{
    const QString grabTo = qEnvironmentVariable("RENAME_GRAB_WINDOW");
    if (grabTo.isEmpty() || !ui || !ui->window || !ui->bridge)
        return;

    if (qEnvironmentVariable("RENAME_GRAB_MODE") == QLatin1String("text"))
        ui->bridge->forceSwitchMode(QStringLiteral("text"));
    const QString filter = qEnvironmentVariable("RENAME_GRAB_FILTER");
    if (!filter.isEmpty())
        ui->bridge->setFilter(filter);
    const QString bufferFile = qEnvironmentVariable("RENAME_GRAB_BUFFER_FILE");
    if (!bufferFile.isEmpty())
    {
        QFile file(bufferFile);
        if (file.open(QIODevice::ReadOnly))
            ui->bridge->setBuffer(QString::fromUtf8(file.readAll()));
    }
    const QStringList rule = qEnvironmentVariable("RENAME_GRAB_RULE").split(QLatin1Char('\n'));
    if (rule.size() == 2)
        ui->bridge->setSingleRule(0, 0, rule[0], rule[1], true);
    if (qEnvironmentVariableIsSet("RENAME_GRAB_VIM"))
        ui->bridge->setVimEnabled(qEnvironmentVariable("RENAME_GRAB_VIM") != QLatin1String("0"));
    // A two-line rule plus a third line is a chain: scope/match stay defaults
    // and the extra rules are literal first-occurrence replaces, enough to
    // screenshot a chain preview.
    if (rule.size() > 2)
    {
        for (int i = 2; i < rule.size(); ++i)
        {
            ui->bridge->addRule();
            ui->bridge->setRuleAt(ui->bridge->ruleCount() - 1, 0, 0,
                                  rule[i], QString(), true, true);
        }
        ui->bridge->selectRule(0);
    }
    if (qEnvironmentVariable("RENAME_GRAB_APPLY") == QLatin1String("1"))
        ui->bridge->apply();
    if (qEnvironmentVariableIsSet("RENAME_GRAB_SELECT"))
    {
        if (auto* table = ui->window->findChild<QTableView*>())
            table->selectRow(0);
    }

    QTimer::singleShot(900, ui->window, [window = ui->window, grabTo] {
        const QPixmap pixmap = window->grab();
        const bool saved = pixmap.save(grabTo, "PNG");
        std::fprintf(stderr, "[rename] grab %s: %s (%dx%d)\n",
                     saved ? "written" : "FAILED", grabTo.toUtf8().constData(),
                     pixmap.width(), pixmap.height());
        std::fflush(stderr);
        QCoreApplication::quit();
    });
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
    ui->window->activateWindow();
    GrabIfAsked(ui);
}

void ShowFolderNow(const QString& folder, bool recursive)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->window || !ui->bridge)
        return;

    ui->bridge->setFolder(folder, recursive);
    ui->window->show();
    ui->window->raise();
    ui->window->activateWindow();
    GrabIfAsked(ui);
}

void ApplyLookNow(const std::string& locale, bool dark, const std::string& uiSize,
                  const std::string& nativeLookJson)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->bridge)
        return;
    ui->bridge->ApplyAppearance(QString::fromStdString(locale), dark,
                                QString::fromStdString(uiSize), ParseLook(nativeLookJson));
    if (ui->window)
        ui->window->ApplyAppearance();
}

} // namespace

bool Start(int argc, char** argv, const std::wstring& dataDir, std::string& error)
{
    auto ui = std::make_unique<Ui>();

    // The plugin path enters without an application; the standalone wrapper
    // (rename-ui.exe) already made a QApplication. Reuse it instead of creating
    // a second one, which Qt forbids.
    if (QCoreApplication::instance())
    {
        ui->app = qobject_cast<QApplication*>(QCoreApplication::instance());
    }
    else
    {
        ui->app = new QApplication(argc, argv);
        ui->ownsApp = true;
    }
    if (!ui->app)
    {
        error = "the existing Qt application is not a QApplication";
        return false;
    }

    ui->app->setApplicationName(QStringLiteral("Rename"));
    if (ui->ownsApp)
        ui->app->setQuitOnLastWindowClosed(false);
    // Fusion keeps the palette and the checkboxes consistent across Windows
    // versions; the window draws its own surfaces from the host's tokens.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    rename_win::InstallNfcNormalizer();

    ui->bridge = new rename_qml::Bridge();
    if (!dataDir.empty())
        ui->bridge->SetDataDir(dataDir);

    Instance() = std::move(ui);
    return true;
}

void Show(const std::vector<std::wstring>& paths, bool recursive)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->bridge)
        return;

    QMetaObject::invokeMethod(ui->bridge, [paths, recursive] {
        Ui* current = Instance().get();
        if (!current || !current->bridge)
            return;
        std::string error;
        if (!BuildWindow(error))
        {
            std::fprintf(stderr, "[rename] window failed to load: %s\n", error.c_str());
            std::fflush(stderr);
            return;
        }
        ShowNow(paths, recursive);
    }, Qt::QueuedConnection);
}

void ShowFolder(const std::wstring& folder, bool recursive)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->bridge)
        return;

    const QString path = ToQt(folder);
    QMetaObject::invokeMethod(ui->bridge, [path, recursive] {
        std::string error;
        if (!BuildWindow(error))
        {
            std::fprintf(stderr, "[rename] window failed to load: %s\n", error.c_str());
            std::fflush(stderr);
            return;
        }
        ShowFolderNow(path, recursive);
    }, Qt::QueuedConnection);
}

void ApplyLook(const std::string& locale, bool dark, const std::string& uiSize,
               const std::string& nativeLookJson)
{
    Ui* ui = Instance().get();
    if (!ui || !ui->bridge)
        return;
    QMetaObject::invokeMethod(
        ui->bridge,
        [locale, dark, uiSize, nativeLookJson] { ApplyLookNow(locale, dark, uiSize, nativeLookJson); },
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

    delete ui->window;
    ui->window = nullptr;
    delete ui->bridge;
    ui->bridge = nullptr;
    if (ui->ownsApp)
        delete ui->app;
    Instance().reset();
    return rc;
}

void Quit()
{
    if (QCoreApplication* app = QCoreApplication::instance())
        QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection);
}

} // namespace rename_host
