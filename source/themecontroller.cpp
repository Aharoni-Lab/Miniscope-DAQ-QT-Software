#include "themecontroller.h"

#include <QQmlEngine>
#include <QUrl>

bool ThemeController::s_dark = true;
QVector<ThemeController *> ThemeController::s_instances;

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
    // Every engine's instance reads the same static, so notify them all.
    const auto instances = s_instances; // copy: a handler could close a window
    for (ThemeController *tc : instances)
        emit tc->darkChanged();
}

void registerMiniscopeQmlTypes()
{
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
