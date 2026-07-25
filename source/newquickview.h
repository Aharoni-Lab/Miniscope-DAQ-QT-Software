#ifndef NEWQUICKVIEW_H
#define NEWQUICKVIEW_H

#include <QObject>
#include <QQuickView>

// QQuickView with an optional aspect-ratio lock on interactive resizing.
// (Close handling moved to the pane host: closing a floating pane re-docks
// it via QWindow::visibleChanged, so no close signal is needed here.)
class NewQuickView: public QQuickView {
    Q_OBJECT
public:
    NewQuickView(QUrl url):
        QQuickView(url) {}

    // Lock interactive (border-drag) resizing to a fixed width:height ratio of the
    // client area. Pass 0 (the default) to leave the window freely resizable.
    void setLockedAspectRatio(qreal ratio) { m_aspectRatio = ratio; }

#ifdef Q_OS_WINDOWS
protected:
    // Constrains live resizing to m_aspectRatio by adjusting the proposed window
    // rect on WM_SIZING (see newquickview.cpp).
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#else
protected:
    // Cross-platform (X11 / Wayland / macOS) aspect-ratio lock: after the window
    // manager resizes us, snap the height back so width:height keeps m_aspectRatio.
    // There is no portable equivalent of Windows' WM_SIZING to constrain the live
    // drag, so we correct once the new size arrives (see newquickview.cpp).
    void resizeEvent(QResizeEvent *e) override;
#endif

private:
    qreal m_aspectRatio = 0.0;       // 0 => unlocked (free resize)
    bool m_adjustingResize = false;  // re-entrancy guard for resizeEvent correction

};

#endif // NEWQUICKVIEW_H
