#include "themecontroller.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QStyleHints>
#include <QUrl>

bool ThemeController::s_dark = true;
QVector<ThemeController *> ThemeController::s_instances;

// Keep the OS-level color scheme in step with the app theme so native pieces
// (menus, dialogs, palette-styled controls) match. All app QML draws from the
// Theme singleton, so this only affects what Qt derives from the palette.
static void applyOsColorScheme(bool dark)
{
    if (QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        app->styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark
                                               : Qt::ColorScheme::Light);
}

ThemeController::ThemeController(QObject *parent)
    : QObject(parent)
{
    s_instances.append(this);
}

ThemeController::~ThemeController()
{
    s_instances.removeAll(this);
}

void ThemeController::setDark(bool dark)
{
    if (s_dark == dark)
        return;
    s_dark = dark;
    applyOsColorScheme(dark);
    // Every engine's instance reads the same static, so notify them all.
    const auto instances = s_instances; // copy: a handler could close a window
    for (ThemeController *tc : instances)
        emit tc->darkChanged();
}

void registerMiniscopeQmlTypes()
{
    // Apply the default (dark) scheme up front; AppShell then restores the
    // saved preference through ThemeState.dark, which re-applies it.
    applyOsColorScheme(ThemeController().dark());

    // One ThemeState per engine (Qt forbids sharing an instance), synced
    // through ThemeController's static state...
    qmlRegisterSingletonType<ThemeController>(
        "Miniscope.Theme", 1, 0, "ThemeState",
        [](QQmlEngine *, QJSEngine *) -> QObject * { return new ThemeController(); });
    // ...and the design-token singleton that derives from it (also one per
    // engine, all tracking the same dark flag).
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Theme.qml")),
                             "Miniscope.Theme", 1, 0, "Theme");
}
