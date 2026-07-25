#include "themecontroller.h"

#include <QQmlEngine>
#include <QUrl>

ThemeController *ThemeController::instance()
{
    static ThemeController controller;
    return &controller;
}

void ThemeController::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    emit darkChanged();
}

void registerMiniscopeQmlTypes()
{
    // The shared theme state (one instance across all engines)...
    qmlRegisterSingletonInstance("Miniscope.Theme", 1, 0, "ThemeState",
                                 ThemeController::instance());
    // ...and the design-token singleton that derives from it. Each engine gets
    // its own Theme instance, but they all bind to the same ThemeState.
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Theme.qml")),
                             "Miniscope.Theme", 1, 0, "Theme");
}
