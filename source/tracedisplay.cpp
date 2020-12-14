#include "tracedisplay.h"
#include "newquickview.h"

#include <QObject>
#include <QGuiApplication>
#include <QScreen>

#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLTexture>

TraceDisplayBackend::TraceDisplayBackend(QObject *parent, QJsonObject ucTraceDisplay):
    QObject(parent)
{
    m_ucTraceDisplay = ucTraceDisplay;

    // Create Window
    createView();
}

void TraceDisplayBackend::createView()
{
//    QScreen *screen = QGuiApplication::primaryScreen();
//    QRect  screenGeometry = screen->geometry();
//    int height = screenGeometry.height();
//    int width = screenGeometry.width();

    qmlRegisterType<TraceDisplay>("TraceDisplay", 1, 0, "TraceDisplay");

    const QUrl url(QStringLiteral("qrc:/TraceDisplayWindow.qml"));
    view = new NewQuickView(url);

//    view->setWidth(400);
//    view->setHeight(height*0.9);
    view->setTitle("Trace Window");

    view->setWidth(m_ucTraceDisplay["width"].toInt(640));
    view->setHeight(m_ucTraceDisplay["height"].toInt(480));

    view->setX(m_ucTraceDisplay["windowX"].toInt(1));
    view->setY(m_ucTraceDisplay["windowY"].toInt(1));

#ifdef Q_OS_WINDOWS
    view->setFlags(Qt::Window | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint);
#endif
    view->show();
}

void TraceDisplayBackend::close()
{
    view->close();
}

TraceDisplay::TraceDisplay()
    : m_t(0),
      m_renderer(nullptr)
{
//    setAcceptedMouseButtons(Qt::AllButtons);
    connect(this, &QQuickItem::windowChanged, this, &TraceDisplay::handleWindowChanged);
}


void TraceDisplay::setT(qreal t)
{
    if (t == m_t)
        return;
    m_t = t;
    emit tChanged();
    if (window())
        window()->update();
}

void TraceDisplay::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &TraceDisplay::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &TraceDisplay::cleanup, Qt::DirectConnection);
//! [1]
        // If we allow QML to do the clearing, they would clear what we paint
        // and nothing would show.
//! [3]
        win->setClearBeforeRendering(false);
    }
}

void TraceDisplay::sync()
{
    if (!m_renderer) {
        m_renderer = new TraceDisplayRenderer();
//        m_renderer->setShowSaturation(m_showSaturation);
//        m_renderer->setDisplayFrame(QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &TraceDisplayRenderer::paint, Qt::DirectConnection);
    }
    m_renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
//    m_renderer->setT(m_t);
//    m_renderer->setDisplayFrame(m_displayFrame);
    m_renderer->setWindow(window());
}

void TraceDisplay::cleanup()
{
    if (m_renderer) {
        delete m_renderer;
        m_renderer = nullptr;
    }
}


TraceDisplayRenderer::~TraceDisplayRenderer()
{
    delete m_program;
    delete m_texture;

    delete m_programGrid;
}

void TraceDisplayRenderer::paint()
{
    qDebug() << "1111111";
    if (!m_programGrid) {

        initializeOpenGLFunctions();

        m_programGrid = new QOpenGLShaderProgram();
        m_programGrid->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/grid.vert");
        m_programGrid->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/grid.frag");
//        m_programGrid->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/imageBasic.vert");
//        m_programGrid->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/imageSaturationScaling.frag");
//        m_programGrid->bindAttributeLocation("position", 0);
//        m_programGrid->bindAttributeLocation("texcoord", 1);
        m_programGrid->link();
    }
//    qDebug() << "22222222";
    m_programGrid->bind();

//    float pan[] = {0.0, 0.0};
//    float scale[] = {1.0, 1.0};
//    float magnify[] = {1.0, 1.0};
//    float spacing = 1.0;

//    m_programGrid->setUniformValueArray("u_pan", pan, 1, 2);
//    m_programGrid->setUniformValueArray("u_scale", scale, 1, 2);
//    m_programGrid->setUniformValueArray("u_magnify", magnify, 1, 2);
//    m_programGrid->setUniformValue("u_spacing", spacing);

    m_programGrid->enableAttributeArray("a_position");
    m_programGrid->enableAttributeArray("a_color");
//    m_programGrid->enableAttributeArray("a_index");

    float position[] = {
        -1, -1,
        0, 0,
        1, 1
    };

    float color[] = {
        0.7, 0.7, 0.7,
        0.7, 0.7, 0.7,
        0.7, 0.7, 0.7
    };
    float index[] = {
        0,
        1,
        2
    };

    m_programGrid->setAttributeArray("a_position", GL_FLOAT, position, 2);
    m_programGrid->setAttributeArray("a_color", GL_FLOAT, color, 3);
//    m_programGrid->setAttributeArray("a_index", GL_FLOAT, index, 1);


//    qDebug() << m_viewportSize.width() <<  m_viewportSize.height();
    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());

    glDisable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glDrawArrays(GL_LINE_STRIP, 0, 3);
//    glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

    m_programGrid->disableAttributeArray("a_position");
    m_programGrid->disableAttributeArray("a_color");
    m_programGrid->disableAttributeArray("a_index");
    m_programGrid->release();

//    // Not strictly needed for this example, but generally useful for when
//    // mixing with raw OpenGL.
    m_window->resetOpenGLState();

}
