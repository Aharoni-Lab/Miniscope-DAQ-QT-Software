#ifndef THEMECONTROLLER_H
#define THEMECONTROLLER_H

#include <QObject>
#include <QVector>

// Process-wide theme state. Every window in this app runs its own QML engine
// (the main shell plus one QQuickView per device/panel), and Qt forbids
// sharing one singleton QObject across engines ("must only be accessed from
// one engine"). So each engine gets its OWN ThemeController instance via the
// singleton factory, and the instances sync through shared static state: a
// write to any of them updates all, so every window's Theme follows the
// shell's dark/light toggle. GUI-thread only (QML engines all live there).
class ThemeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)

public:
    explicit ThemeController(QObject *parent = nullptr);
    ~ThemeController() override;

    bool dark() const { return s_dark; }
    void setDark(bool dark); // updates every engine's instance

signals:
    void darkChanged();

private:
    static bool s_dark;
    static QVector<ThemeController *> s_instances;
};

// Register the app's QML types: the per-engine ThemeState singleton and the
// Theme.qml design-token singleton (module Miniscope.Theme). Call once before
// the first QML engine loads - from main(), and from any test that loads app QML.
void registerMiniscopeQmlTypes();

#endif // THEMECONTROLLER_H
