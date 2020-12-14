#ifndef TRACEDISPLAY_H
#define TRACEDISPLAY_H

#include "newquickview.h"

#include <QtQuick/QQuickItem>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLTexture>

#include <QJsonObject>


class TraceDisplayBackend : public QObject
{
    Q_OBJECT
public:
    TraceDisplayBackend(QObject *parent = nullptr, QJsonObject ucTraceDisplay = QJsonObject());
    void createView();

public slots:
    void close();

private:
    NewQuickView *view;
    QJsonObject m_ucTraceDisplay;
};

class TraceDisplayRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    TraceDisplayRenderer() :
        m_program(nullptr),
        m_texture(nullptr),
        m_t(0),
        m_programGrid(nullptr)
    { }
    ~TraceDisplayRenderer();

    void setT(qreal t) { m_t = t; }
    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setWindow(QQuickWindow *window) { m_window = window; }

//signals:

public slots:
    void paint();

private:
    QSize m_viewportSize;
    QOpenGLShaderProgram *m_program;
    QOpenGLTexture *m_texture;
    QQuickWindow *m_window;

    qreal m_t;

    // Programs used for trace display
    QOpenGLShaderProgram *m_programGrid;

};

class TraceDisplay : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)

public:
    TraceDisplay();
    qreal t() const { return m_t; }
    void setT(qreal t);


signals:
    void tChanged();

public slots:
    void sync();
    void cleanup();

private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    qreal m_t;
    TraceDisplayRenderer *m_renderer;

};

#endif // TRACEDISPLAY_H