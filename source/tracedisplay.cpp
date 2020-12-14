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
    m_programGridH(nullptr)
{
    windowSize = 5; // in seconds. Consider having this defined in user config!
    gridSpacingV = 0.25; // in seconds

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
}

void TraceDisplayRenderer::initPrograms()
{
    if (!m_programGridV) {

        initializeOpenGLFunctions();

        m_programGridV = new QOpenGLShaderProgram();
        m_programGridV->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/grid.vert");
        m_programGridV->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/grid.frag");

        m_programGridV->link();
    }
}

void TraceDisplayRenderer::updateGridV()
{
    float spacing = 0.25f;

    m_programGridV->bind();

    // These will be moved to mouse and keyboard slots
    m_programGridV->setUniformValueArray("u_pan", pan, 1, 2);
    m_programGridV->setUniformValueArray("u_scale", scale, 1, 2);
    m_programGridV->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programGridV->setUniformValue("u_spacing", spacing);
    // ----------------------------------------------------------

//    self.VGridSpacing = 0.25  # in seconds
//    self.numVGridLines = int((self.windowSize/self.VGridSpacing) + 1)
//    gridX = np.repeat(np.linspace(-1, 1, self.numVGridLines, dtype=np.float32), 2)
//    gridY = np.tile(np.array([-1., 1.], dtype=np.float32), self.numVGridLines)
//    self.programGridV["a_position"] = np.stack((gridX, gridY), axis=-1)
//    self.programGridV["a_color"] = np.tile(np.array([0.7, 0.7, 0.7], dtype=np.float32), (self.numVGridLines*2, 1))
//    self.programGridV["a_index"] = np.repeat(np.arange(self.numVGridLines, dtype=np.float32) * self.VGridSpacing, 2)
//    self.programGridV["u_spacing"] = self.VGridSpacing

    int numVGridLines = (int)((windowSize / gridSpacingV) + 1);
    float gridLineStep = 2.0 / (float)numVGridLines;

    position.clear();
    index.clear();
    color.clear();

    float idx = 0;
    for (float x = -1; x <= 1; x+= (gridLineStep) ) {
        position.append({x, -1.0f, x, 1.0f});

        color.append({0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f});

        index.append({idx, idx});
        idx += gridSpacingV;
    }

    m_programGridV->setAttributeArray("a_position", GL_FLOAT, &position[0], 2);
    m_programGridV->setAttributeArray("a_color", GL_FLOAT, &color[0], 3);
    m_programGridV->setAttributeArray("a_index", GL_FLOAT, &index[0], 1);

    m_programGridV->release();
}

void TraceDisplayRenderer::drawGridV()
{
    m_programGridV->bind();

    m_programGridV->enableAttributeArray("a_position");
    m_programGridV->enableAttributeArray("a_color");
    m_programGridV->enableAttributeArray("a_index");

    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());

    glDisable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glLineWidth(3);
    glDrawArrays(GL_LINE_STRIP, 0, index.length());
    qDebug() << (index.length());

//    glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

    m_programGridV->disableAttributeArray("a_position");
    m_programGridV->disableAttributeArray("a_color");
    m_programGridV->disableAttributeArray("a_index");

    m_programGridV->release();

}
void TraceDisplayRenderer::paint()
{

    updateGridV();

    drawGridV();
//    // Not strictly needed for this example, but generally useful for when
//    // mixing with raw OpenGL.
    m_window->resetOpenGLState();

}
