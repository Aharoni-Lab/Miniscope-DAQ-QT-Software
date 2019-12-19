#include "videodisplay.h"

#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLTexture>

#include <QImage>

//! [7]
VideoDisplay::VideoDisplay()
    : m_t(0)
    , m_renderer(nullptr)
{
//    m_displayFrame2.load("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png");
    connect(this, &QQuickItem::windowChanged, this, &VideoDisplay::handleWindowChanged);
}
//! [7]

//! [8]
void VideoDisplay::setT(qreal t)
{
    if (t == m_t)
        return;
    m_t = t;
    emit tChanged();
    if (window())
        window()->update();
}
void VideoDisplay::setDisplayFrame(QImage frame) {
    m_displayFrame2 = frame.copy();
    if (m_renderer)
        m_renderer->setDisplayFrame(m_displayFrame2);
}
//void VideoDisplayRenderer::setDisplayFrame(QImage frame) {
//    m_displayFrame = frame;
//}
//! [8]

//! [1]
void VideoDisplay::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &VideoDisplay::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &VideoDisplay::cleanup, Qt::DirectConnection);
//! [1]
        // If we allow QML to do the clearing, they would clear what we paint
        // and nothing would show.
//! [3]
        win->setClearBeforeRendering(false);
    }
}
//! [3]

//! [6]
void VideoDisplay::cleanup()
{
    if (m_renderer) {
        delete m_renderer;
        m_renderer = nullptr;
    }
}

VideoDisplayRenderer::~VideoDisplayRenderer()
{
    delete m_program;
    delete m_texture;
}
//! [6]

//! [9]
void VideoDisplay::sync()
{
    if (!m_renderer) {
        m_renderer = new VideoDisplayRenderer();
//        m_renderer->setDisplayFrame(QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &VideoDisplayRenderer::paint, Qt::DirectConnection);
    }
    m_renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
//    m_renderer->setT(m_t);
//    m_renderer->setDisplayFrame(m_displayFrame);
    m_renderer->setWindow(window());
}
//! [9]

//! [4]
void VideoDisplayRenderer::paint()
{
//    qDebug() << "Painting!";
    if (!m_program) {
        initializeOpenGLFunctions();

        m_program = new QOpenGLShaderProgram();
        m_program->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex,
                                                    "attribute highp vec2 position;"
                                                    "attribute highp vec2 texcoord;"
                                                    "varying highp vec2 v_texcoord;"
                                                    "void main() {"
                                                    "   gl_Position = vec4(position, 0.0, 1.0);"
                                                    "   v_texcoord = texcoord;"
                                                    "}");
        m_program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment,
                                                    "uniform sampler2D texture;"
                                                    "varying highp vec2 v_texcoord;"
                                                    "uniform lowp float alpha;"
                                                    "uniform lowp float beta;"
                                                    "uniform lowp float showSaturation;"
                                                    "void main() {"
                                                    "   gl_FragColor = texture2D(texture, v_texcoord);"
                                                    "   if (gl_FragColor.b == 1.0 && showSaturation == 1.0)"
                                                    "       gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0);"
                                                    "   else"
                                                    "       gl_FragColor.rgb = gl_FragColor.rgb * alpha - beta;"
                                                    "   lowp float temp = gl_FragColor.r;"
                                                    "   gl_FragColor.r = gl_FragColor.b;"
                                                    "   gl_FragColor.b = temp;"
                                                    "}");

        m_program->bindAttributeLocation("position", 0);
        m_program->bindAttributeLocation("texcoord", 1);
        m_program->link();

        m_texture = new QOpenGLTexture(QImage(":/img/MiniscopeLogo.png"));
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
    m_program->setUniformValue("alpha", (float) 1);
    m_program->setUniformValue("beta", (float) 0);
    m_program->setUniformValue("showSaturation", (float) 1);

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
        m_newFrame = false;
        m_texture->destroy();
        m_texture->create();
        m_texture->setData(m_displayFrame);
    }
}
//! [5]
