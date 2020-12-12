#include "tracedisplay.h"

#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLTexture>

TraceDisplay::TraceDisplay()
    : m_t(0)
{
    setAcceptedMouseButtons(Qt::AllButtons);
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
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &VideoDisplayRenderer::paint, Qt::DirectConnection);
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
}

void TraceDisplayRenderer::paint()
{
//    qDebug() << "Painting!";
    if (!m_program) {
        initializeOpenGLFunctions();

        m_program = new QOpenGLShaderProgram();
        m_program->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/imageBasic.vert");
        m_program->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/imageSaturationScaling.frag");


        m_program->bindAttributeLocation("position", 0);
        m_program->bindAttributeLocation("texcoord", 1);
        m_program->link();

        m_texture = new QOpenGLTexture(QImage(":/img/MiniscopeLogo.png").rgbSwapped());
        m_texture->bind(0);



    }
//! [4] //! [5]

    m_texture->bind(0);

    m_program->bind();

    m_program->setUniformValue( "texture", 0 ); // <----- texture unit

    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);

    float position[] = {
        -1, -1,
        1, -1,
        -1, 1,
        1, 1

    };
    float texcoord[] = {
//        1, 1,
//        1, 0,
//        0, 1,
//        0, 0
        0, 1,
        1, 1,
        0, 0,
        1, 0

    };
    m_program->setAttributeArray(0, GL_FLOAT, position, 2);
    m_program->setAttributeArray(1, GL_FLOAT, texcoord, 2);
    m_program->setUniformValue("alpha", (float) m_alpha);
    m_program->setUniformValue("beta", (float) m_beta);
    m_program->setUniformValue("showSaturation", (float) m_showStaturation);

    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());

    glDisable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);
    m_program->release();

    // Not strictly needed for this example, but generally useful for when
    // mixing with raw OpenGL.
    m_window->resetOpenGLState();

    if (m_newFrame) {
//        qDebug() << "Set new texture QImage";

        m_texture->destroy();
        m_texture->create();
        m_texture->setData(m_displayFrame);
        m_newFrame = false;
    }

}
