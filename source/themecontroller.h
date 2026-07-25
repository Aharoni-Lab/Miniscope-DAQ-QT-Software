#ifndef THEMECONTROLLER_H
#define THEMECONTROLLER_H

#include <QObject>

// Process-wide theme state. Every window in this app runs its own QML engine
// (the main shell plus one QQuickView per device/panel), and QML singletons
// like Theme.qml are instantiated PER ENGINE - so a bare QML property could
// never keep the windows' themes in sync. This C++ singleton is registered
// into the QML type system once (registerMiniscopeQmlTypes) and shared by
// every engine; Theme.qml derives its `dark` flag from it.
class ThemeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)

public:
    static ThemeController *instance();

    bool dark() const { return m_dark; }
    void setDark(bool dark);

signals:
    void darkChanged();

private:
    explicit ThemeController(QObject *parent = nullptr) : QObject(parent) {}
    bool m_dark = true;
};

// Register the app's QML types: the ThemeState C++ singleton and the Theme.qml
// design-token singleton (module Miniscope.Theme). Call once before the first
// QML engine loads - from main(), and from any test that loads app QML.
void registerMiniscopeQmlTypes();

#endif // THEMECONTROLLER_H
