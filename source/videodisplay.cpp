#include "videodisplay.h"

#include <QtQuick/qquickwindow.h>
#include <QtOpenGL/QOpenGLShaderProgram>   // Qt6: moved from QtGui to Qt6::OpenGL
#include <QtGui/QOpenGLContext>
#include <QtOpenGL/QOpenGLTexture>   // Qt6: moved from QtGui to Qt6::OpenGL

#include <QImage>

//! [7]
VideoDisplay::VideoDisplay()
    : m_t(0),
      m_acqFPS(0),
      m_renderer(nullptr),
      m_lutMode(0),
      m_roiSelectionActive(false),
      m_addTraceRoiSelectionActive(false),   // was uninitialized: garbage-true made a
                                             // plain click on the video spawn a neuron
      m_ROI({0,0,10,10,0}),
      m_hasPressPos(false)
{
//    m_displayFrame2.load("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png");
    setAcceptedMouseButtons(Qt::AllButtons);
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
//    m_displayFrame2 = frame;
    // Checking to see if there is already a new frame waiting has solved a
    // consistent source of crash.
    // This crash was due to the texture data of the next frame in the queue to be displayed
    // changing before it is moved to GPU memory I think.
    if (m_renderer && !m_renderer->m_newFrame)
        m_renderer->setDisplayFrame(frame);
//    else
//        qDebug() << "New frame available before last frame was displayed";
}

void VideoDisplay::setShowSaturation(double value)
{
    m_showSaturation = value;
    if (m_renderer) {
        m_renderer->setShowSaturation(value);
    }
}

void VideoDisplay::setLutMode(double value)
{
    m_lutMode = value;
    if (m_renderer) {
        m_renderer->setLutMode(value);
    }
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
        // Qt6: QQuickWindow::setClearBeforeRendering() was removed. Instead set the
        // window clear color; the scene graph clears to it at the start of the
        // render pass and our underlay (drawn during beforeRenderPassRecording)
        // paints on top of that, beneath the QML content.
        win->setColor(Qt::black);
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
        m_renderer->setShowSaturation(m_showSaturation);
        m_renderer->setLutMode(m_lutMode);
//        m_renderer->setDisplayFrame(QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
        // Qt6: draw during render-pass recording. beforeRendering now fires before
        // the render pass begins, when there is no bound framebuffer to draw into,
        // so an underlay must use beforeRenderPassRecording instead.
        connect(window(), &QQuickWindow::beforeRenderPassRecording, m_renderer, &VideoDisplayRenderer::paint, Qt::DirectConnection);
    }
    m_renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
//    m_renderer->setT(m_t);
//    m_renderer->setDisplayFrame(m_displayFrame);
    m_renderer->setWindow(window());
}
//! [9]

void VideoDisplay::mousePressEvent(QMouseEvent *event){
    if ((m_roiSelectionActive || m_addTraceRoiSelectionActive) && event->button() == Qt::LeftButton) {
        // TODO: Send info to shader to draw rectangle
        // Qt6: QMouseEvent::x()/y() are deprecated (use position()), and cloning a
        // pointer event is no longer the pattern. Store the press point instead.
        m_pressPos = event->position().toPoint();
        m_hasPressPos = true;
    }
//        qDebug() << "Mouse Press" << event;
}

void VideoDisplay::mouseMoveEvent(QMouseEvent *event) {
    if (!m_hasPressPos)
        return;

    const QPoint pos = event->position().toPoint();
    const int leftEdge = qMin(m_pressPos.x(), pos.x());
    const int topEdge  = qMin(m_pressPos.y(), pos.y());
    const int width    = qAbs(m_pressPos.x() - pos.x());
    const int height   = qAbs(m_pressPos.y() - pos.y());

    if (m_roiSelectionActive) {
        setROI({leftEdge,topEdge,width,height,m_roiSelectionActive});
    }
    if (m_addTraceRoiSelectionActive) {
        setAddTraceROI({leftEdge,topEdge,width,height,m_addTraceRoiSelectionActive});
    }
}

void VideoDisplay::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_hasPressPos) {
        const QPoint pos = event->position().toPoint();

        // Calculate ROI properties
        int leftEdge = qMin(m_pressPos.x(), pos.x());
        int topEdge  = qMin(m_pressPos.y(), pos.y());
        int width    = qAbs(m_pressPos.x() - pos.x());
        int height   = qAbs(m_pressPos.y() - pos.y());

        if (m_roiSelectionActive ) {
            m_roiSelectionActive = false;
            // Send new ROI to behavior camera class
            emit newROISignal(leftEdge, topEdge, width, height);
            setROI({leftEdge,topEdge,width,height,m_roiSelectionActive});

        }
        if (m_addTraceRoiSelectionActive ) {
            m_addTraceRoiSelectionActive = false;
            // Send new ROI to behavior camera class
            if (width < 5 && height < 5) {
                leftEdge -= 3;
                topEdge -= 3;
                width = 7;
                height = 7;
            }
            emit newAddTraceROISignal(leftEdge, topEdge, width, height);
            setAddTraceROI({leftEdge,topEdge,width,height,m_addTraceRoiSelectionActive});

        }
    }
    // Reset stored press position
    m_hasPressPos = false;
}

void VideoDisplay::setROI(QList<int> roi)
{
    m_ROI = roi;

    roiChanged();
}

void VideoDisplay::setAddTraceROI(QList<int> roi)
{
    m_addTraceROI = roi;

    addTraceROIChanged();
}

void VideoDisplayRenderer::paint()
{
    //    qDebug() << "Painting!";

    // Qt6: bracket all raw OpenGL with begin/endExternalCommands so the RHI-based
    // scene graph knows its cached GL state is being touched. This replaces the
    // old m_window->resetOpenGLState() call.
    m_window->beginExternalCommands();

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
    m_program->setUniformValue("lutMode", (float) m_lutMode);

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

    if (m_newFrame) {
//        qDebug() << "Set new texture QImage";

        m_texture->destroy();
        m_texture->create();
        m_texture->setData(m_displayFrame);
        m_newFrame = false;
    }

    // Qt6: replaces the removed m_window->resetOpenGLState().
    m_window->endExternalCommands();
}
//! [5]
