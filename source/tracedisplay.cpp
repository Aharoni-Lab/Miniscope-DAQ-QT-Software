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

TraceDisplayRenderer::TraceDisplayRenderer() :
    m_program(nullptr),
    m_texture(nullptr),
    m_t(0),
    m_programGridV(nullptr),
    m_programGridH(nullptr),
    m_programMovingBar(nullptr)
{
    windowSize = 5; // in seconds. Consider having this defined in user config!
    gridSpacingV = .25; // in seconds

    pan[0] = 0; pan[1] = 0;
    scale[0] = 1; scale[1] = 1;
    magnify[0] = 1; magnify[1] = 1;

    initPrograms();
}

TraceDisplayRenderer::~TraceDisplayRenderer()
{
    delete m_program;
    delete m_texture;

    delete m_programGridV;
    delete m_programGridH;
    delete m_programMovingBar;
}

void TraceDisplayRenderer::initPrograms()
{

    initializeOpenGLFunctions();

    // Vertical lines program
    m_programGridV = new QOpenGLShaderProgram();
    m_programGridV->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/grid.vert");
    m_programGridV->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/grid.frag");
    m_programGridV->link();

    // Moving bar program
    m_programMovingBar = new QOpenGLShaderProgram();
    m_programMovingBar->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/movingBar.vert");
    m_programMovingBar->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/movingBar.frag");
    m_programMovingBar->link();

    updateMovingBar();


}

void TraceDisplayRenderer::updateGridV()
{

    m_programGridV->bind();

    // ------ These will be moved to mouse and keyboard slots
    m_programGridV->setUniformValueArray("u_pan", pan, 1, 2);
    m_programGridV->setUniformValueArray("u_scale", scale, 1, 2);
    m_programGridV->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programGridV->setUniformValue("u_spacing", gridSpacingV);
    // ----------------------------------------------------------

    int numVGridLines = (int)((windowSize / gridSpacingV) + 1);
    float gridLineStep = 2.0 / ((float)numVGridLines - 1);

    gridVPosition.clear();
    girdVIndex.clear();
    gridVColor.clear();

    float idx = 0;
    for (float x = -1; x <= 1; x+= (gridLineStep) ) {
        gridVPosition.append({x, -1.0f,
                         x, 1.0f});

        gridVColor.append({0.9f, 0.9f, 0.9f,
                      0.9f, 0.9f, 0.9f});

        girdVIndex.append({idx,
                      idx});
        idx += gridSpacingV;
    }

    m_programGridV->setAttributeArray("a_position", GL_FLOAT, &gridVPosition[0], 2);
    m_programGridV->setAttributeArray("a_color", GL_FLOAT, &gridVColor[0], 3);
    m_programGridV->setAttributeArray("a_index", GL_FLOAT, &girdVIndex[0], 1);

    m_programGridV->release();
}

void TraceDisplayRenderer::updateMovingBar()
{
    currentTime = 0;
    startTime = 0;
    m_programMovingBar->bind();

    // ------ These will be moved to mouse and keyboard slots
    m_programMovingBar->setUniformValueArray("u_pan", pan, 1, 2);
    m_programMovingBar->setUniformValueArray("u_scale", scale, 1, 2);
    m_programMovingBar->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programMovingBar->setUniformValue("u_time", currentTime - startTime);
    m_programMovingBar->setUniformValue("u_windowSize", windowSize);
    // ----------------------------------------------------------

    movingBarPosition = {0.0f, -1.0f, 0.0f, 1.0f};
    movingBarColor = {0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f};
    m_programMovingBar->setAttributeArray("a_position", GL_FLOAT, &movingBarPosition[0], 2);
    m_programMovingBar->setAttributeArray("a_color", GL_FLOAT, &movingBarColor[0], 3);

    m_programMovingBar->release();
}

void TraceDisplayRenderer::drawGridV()
{
    m_programGridV->bind();

    m_programGridV->enableAttributeArray("a_position");
    m_programGridV->enableAttributeArray("a_color");
    m_programGridV->enableAttributeArray("a_index");



    glLineWidth(3);
    glDrawArrays(GL_LINE_STRIP, 0, girdVIndex.length());
//    qDebug() << (index.length());

//    glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

    m_programGridV->disableAttributeArray("a_position");
    m_programGridV->disableAttributeArray("a_color");
    m_programGridV->disableAttributeArray("a_index");

    m_programGridV->release();

}

void TraceDisplayRenderer::drawMovingBar()
{
    m_programMovingBar->bind();
    currentTime+= 0.06;
    m_programMovingBar->setUniformValue("u_time", currentTime);
    m_programMovingBar->enableAttributeArray("a_position");
    m_programMovingBar->enableAttributeArray("a_color");

    glLineWidth(10);
    glDrawArrays(GL_LINE_STRIP, 0, girdVIndex.length());

    m_programMovingBar->disableAttributeArray("a_position");
    m_programMovingBar->disableAttributeArray("a_color");
    m_programMovingBar->release();
}

void TraceDisplayRenderer::paint()
{

    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());

    glDisable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    updateGridV();

    drawGridV();
    drawMovingBar();
//    // Not strictly needed for this example, but generally useful for when
//    // mixing with raw OpenGL.
    m_window->resetOpenGLState();

}
