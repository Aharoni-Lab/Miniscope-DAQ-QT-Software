#include "newquickview.h"

#ifdef Q_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <QtGlobal>

// Keep interactive resizing locked to m_aspectRatio. WM_SIZING hands us the
// proposed *window* rect (title bar + borders included) and the edge being
// dragged; we correct the rect so the *client* area keeps the locked ratio.
// Working in the native (physical-pixel) RECT and deriving the frame size from
// GetWindowRect/GetClientRect keeps this correct under display scaling.
bool NewQuickView::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (m_aspectRatio > 0.0 && eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_SIZING) {
            RECT *rect = reinterpret_cast<RECT *>(msg->lParam);

            RECT windowRect, clientRect;
            GetWindowRect(msg->hwnd, &windowRect);
            GetClientRect(msg->hwnd, &clientRect); // client origin is (0,0)
            const int frameW = (windowRect.right - windowRect.left) - clientRect.right;
            const int frameH = (windowRect.bottom - windowRect.top) - clientRect.bottom;

            int clientW = (rect->right - rect->left) - frameW;
            int clientH = (rect->bottom - rect->top) - frameH;

            switch (msg->wParam) {
            case WMSZ_LEFT:
            case WMSZ_RIGHT:
                // Dragging a vertical edge: width drives height.
                clientH = qRound(clientW / m_aspectRatio);
                rect->bottom = rect->top + clientH + frameH;
                break;
            case WMSZ_TOP:
            case WMSZ_BOTTOM:
                // Dragging a horizontal edge: height drives width.
                clientW = qRound(clientH * m_aspectRatio);
                rect->right = rect->left + clientW + frameW;
                break;
            default:
                // Dragging a corner: width drives height, growing toward the corner.
                clientH = qRound(clientW / m_aspectRatio);
                if (msg->wParam == WMSZ_TOPLEFT || msg->wParam == WMSZ_TOPRIGHT)
                    rect->top = rect->bottom - clientH - frameH;
                else
                    rect->bottom = rect->top + clientH + frameH;
                break;
            }
            *result = TRUE;
            return true;
        }
    }
    return QQuickView::nativeEvent(eventType, message, result);
}
#else // !Q_OS_WINDOWS

#include <QResizeEvent>
#include <QtGlobal>

// On non-Windows platforms there is no WM_SIZING hook to constrain the live
// drag, so we let the window manager resize freely and then snap the height to
// keep the locked width:height ratio. Width drives height (horizontal drags
// feel natural; vertical/corner drags get corrected to match).
void NewQuickView::resizeEvent(QResizeEvent *e)
{
    QQuickView::resizeEvent(e);

    if (m_aspectRatio <= 0.0 || m_adjustingResize)
        return;

    const QSize s = e->size();
    if (s.width() <= 0 || s.height() <= 0)
        return;

    const int targetH = qRound(s.width() / m_aspectRatio);
    if (targetH > 0 && targetH != s.height()) {
        m_adjustingResize = true;
        resize(QSize(s.width(), targetH));
        m_adjustingResize = false;
    }
}
#endif
