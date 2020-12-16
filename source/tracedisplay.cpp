#include "tracedisplay.h"
#include "newquickview.h"

#include <QObject>
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>

#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLTexture>
#include <QtGui/QOpenGLFramebufferObject>

TraceDisplayBackend::TraceDisplayBackend(QObject *parent, QJsonObject ucTraceDisplay):
    QObject(parent),
    m_traceDisplay(nullptr)
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

    m_traceDisplay = view->rootObject()->findChild<TraceDisplay*>("traceDisplay");
}

void TraceDisplayBackend::addNewTrace(float color[3], float scale, QAtomicInt *displayBufNum, QAtomicInt *numDataInBuf, int bufSize, qint64 *dataT, float *dataY)
{

    trace_t newTrace = trace_t(color, scale, displayBufNum, numDataInBuf, bufSize, dataT, dataY);
    m_traceDisplay->addNewTrace(newTrace);
}

void TraceDisplayBackend::close()
{
    view->close();
}

TraceDisplay::TraceDisplay()
    : m_t(0),
      m_xLabel({"0.0","1.0","2.0","3.0","4.0"}),
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
    //    setXLabel({"0.0","1.0","2.0","3.0","4.0"});
}

void TraceDisplay::addNewTrace(trace_t newTrace)
{

    if(m_renderer) {
        m_renderer->addNewTrace(newTrace);
    }
    else {
        // renderer hasn't been created yet. Store trace info till it is.
        m_tempTraces.append(newTrace);
    }
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
        m_renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
        m_renderer->createFBO();
//        m_renderer->setShowSaturation(m_showSaturation);
//        m_renderer->setDisplayFrame(QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &TraceDisplayRenderer::paint, Qt::DirectConnection);

        for (int i=0; i < m_tempTraces.length(); i++) {
            m_renderer->addNewTrace(m_tempTraces[i]);
        }
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
    m_numTraces(2),
    m_fbo(nullptr),
    m_programGridV(nullptr),
    m_programGridH(nullptr),
    m_programMovingBar(nullptr),
    m_programTraces(nullptr)
{
    windowSize = 5; // in seconds. Consider having this defined in user config!
    gridSpacingV = .25; // in seconds

    pan[0] = 0.0f; pan[1] = 0.0f;
    scale[0] = 1.0f; scale[1] = 1.0f;
    magnify[0] = 1.0f; magnify[1] = 1.0f;

    startTime =  QDateTime().currentMSecsSinceEpoch(); //time software started up

    initPrograms();
    float c[] = {0.5,0.7,1.0};
    bufNum = 0;
    numData[0] = 10;
    numData[1] = 10;
//    for (int a=0; a < 10; a++) {
//        dataY[0][a] = ((float)a)/10.0;
//        dataT[0][a] = ((float)a)/10;
//        dataY[1][a] = ((float)a)/10.0;
//        dataT[1][a] = ((float)a)/10;
//    }

    traces.append(trace_t(c, .1, &bufNum, numData, 10, &dataT[0][0], &dataY[0][0]));

}

TraceDisplayRenderer::~TraceDisplayRenderer()
{
    movingBarVBO.destroy();

    delete m_program;
    delete m_texture;

    delete m_programGridV;
    delete m_programGridH;
    delete m_programMovingBar;
    delete m_programTraces;
}

void TraceDisplayRenderer::initPrograms()
{

    initializeOpenGLFunctions();

    // Vertical lines program
    m_programGridV = new QOpenGLShaderProgram();
    m_programGridV->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/grid.vert");
    m_programGridV->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/grid.frag");
    m_programGridV->link();
    initGridV();

    // Horizontal lines program
    m_programGridH = new QOpenGLShaderProgram();
    m_programGridH->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/grid.vert");
    m_programGridH->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/grid.frag");
    m_programGridH->link();
    initGridH();

    // Moving bar program
    m_programMovingBar = new QOpenGLShaderProgram();
    m_programMovingBar->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/movingBar.vert");
    m_programMovingBar->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/movingBar.frag");
    m_programMovingBar->link();
    initMovingBar();

    // Moving bar program
    m_programTraces = new QOpenGLShaderProgram();
    m_programTraces->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/trace.vert");
    m_programTraces->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/trace.frag");
    m_programTraces->link();
    initTraces();


}

void TraceDisplayRenderer::addNewTrace(trace_t newTrace)
{
    newTrace.offset = 0.0f;
    traces.append(newTrace);

    updateTraceOffsets();
}

void TraceDisplayRenderer::updateTraceOffsets()
{
    int numTraces = traces.length();

    float offsetStep = 2 / ((float)numTraces + 1);

    for (int num=0; num < numTraces; num++) {
        traces[num].offset = -1 + ((num + 1) * offsetStep);
    }
}

void TraceDisplayRenderer::createFBO()
{
    m_fbo = new QOpenGLFramebufferObject(m_viewportSize.width(), m_viewportSize.height());
}

void TraceDisplayRenderer::initGridV()
{
    // Holds position, color, index for vertical grid vertex
    QVector<float> gridVData;

    int numVGridLines = (int)((windowSize / gridSpacingV) + 1);
    float gridLineStep = 2.0 / ((float)numVGridLines - 1);

    float idx = 0;
    for (float x = -1; x <= 1; x+= (gridLineStep) ) {
        // Position
        gridVData.append({x, -1.0f,
                          idx});
        gridVData.append({x, 1.0f,
                          idx});
        idx += gridSpacingV;
    }

    gridVVOB.create();
    gridVVOB.bind();
    gridVVOB.allocate(&gridVData[0], gridVData.length() * sizeof(float));
    gridVVOB.release();
}
void TraceDisplayRenderer::updateGridV()
{

    m_programGridV->bind();

    float gridVColor[] = {0.9f, 0.9f, 0.9f};
    // ------ These will be moved to mouse and keyboard slots
    m_programGridV->setUniformValueArray("u_pan", pan, 1, 2);
    m_programGridV->setUniformValueArray("u_scale", scale, 1, 2);
    m_programGridV->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programGridV->setUniformValueArray("u_color", gridVColor, 1, 3);
    m_programGridV->setUniformValue("u_spacing", gridSpacingV);
    // ----------------------------------------------------------

    // WILL UPDATE VBO HERE IF NEEDED

    m_programGridV->release();
}

void TraceDisplayRenderer::drawGridV()
{
    m_programGridV->bind();
    gridVVOB.bind();

    m_programGridV->enableAttributeArray("a_position");
    m_programGridV->enableAttributeArray("a_index");

    m_programGridV->setAttributeBuffer("a_position",GL_FLOAT, 0,                2, 3 * sizeof(float));
    m_programGridV->setAttributeBuffer("a_index",   GL_FLOAT, 2 * sizeof(float), 1, 3 * sizeof(float));


    glLineWidth(3);
    glDrawArrays(GL_LINE_STRIP, 0, gridVVOB.size()/(3 * sizeof(float)));
//    qDebug() << "SIZE" << (gridVVOB.size());

//    glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

    m_programGridV->disableAttributeArray("a_position");
    m_programGridV->disableAttributeArray("a_index");

    gridVVOB.release();
    m_programGridV->release();

}

void TraceDisplayRenderer::initGridH()
{
    // Holds position, color, index for horizontal grid vertex
    QVector<float> gridHData;

    int numHGridLines = m_numTraces;
    float gridLineStep = 2 / ((float)numHGridLines + 1);

    float idx = 0;
    for (float y = -1; y <= 1; y+= (gridLineStep) ) {
        // Position
        if (idx > 0 && idx <= numHGridLines) {
            gridHData.append({-1.0f, y,
                              idx});
            gridHData.append({1.0f, y,
                              idx});
        }
        idx += 1;
    }

    qDebug() << gridHData;
    gridHVOB.create();
    gridHVOB.bind();
    gridHVOB.allocate(&gridHData[0], gridHData.length() * sizeof(float));
    gridHVOB.release();
}

void TraceDisplayRenderer::updateGridH()
{

    m_programGridH->bind();

    float gridHColor[] = {0.9f, 0.9f, 0.9f};

    // ------ These will be moved to mouse and keyboard slots
    m_programGridH->setUniformValueArray("u_pan", pan, 1, 2);
    m_programGridH->setUniformValueArray("u_scale", scale, 1, 2);
    m_programGridH->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programGridH->setUniformValueArray("u_color", gridHColor, 1, 3);
    m_programGridH->setUniformValue("u_spacing", 1.0f);
    // ----------------------------------------------------------

    // WILL UPDATE VBO HERE IF NEEDED

    m_programGridH->release();
}

void TraceDisplayRenderer::drawGridH()
{
    m_programGridH->bind();
    gridHVOB.bind();

    m_programGridH->enableAttributeArray("a_position");
//    m_programGridH->enableAttributeArray("a_color");
    m_programGridH->enableAttributeArray("a_index");

    m_programGridH->setAttributeBuffer("a_position",GL_FLOAT, 0,                2, 3 * sizeof(float));
    m_programGridH->setAttributeBuffer("a_index",   GL_FLOAT, 2 * sizeof(float), 1, 3 * sizeof(float));


    glLineWidth(3);
    glDrawArrays(GL_LINE_STRIP, 0, gridHVOB.size()/(3 * sizeof(float)));
//    qDebug() << "SIZE" << (gridVVOB.size());

//    glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

    m_programGridH->disableAttributeArray("a_position");
    m_programGridH->disableAttributeArray("a_index");

    gridHVOB.release();
    m_programGridH->release();

}

void TraceDisplayRenderer::initMovingBar()
{
    movingBarVBO.create();
    movingBarVBO.bind();

    float movingBarData[] = {0.0f, -1.0f,
                             0.0f, 1.0f};
    movingBarVBO.allocate(movingBarData, 4 * sizeof(float));
    movingBarVBO.release();
}
void TraceDisplayRenderer::updateMovingBar()
{

    m_programMovingBar->bind();
    float movingBarColor[] = {0.9f, 0.9f, 0.9f};
    // ------ These will be moved to mouse and keyboard slots
    m_programMovingBar->setUniformValueArray("u_pan", pan, 1, 2);
    m_programMovingBar->setUniformValueArray("u_scale", scale, 1, 2);
    m_programMovingBar->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programMovingBar->setUniformValueArray("u_color", movingBarColor, 1, 3);

    m_programMovingBar->setUniformValue("u_windowSize", windowSize);
    // ----------------------------------------------------------
    m_programMovingBar->setUniformValue("u_time", (float)(currentTime - startTime)/1000.0f);
    m_programMovingBar->release();
}

void TraceDisplayRenderer::drawMovingBar()
{
    m_programMovingBar->bind();
    movingBarVBO.bind();

    m_programMovingBar->enableAttributeArray("a_position");
    m_programMovingBar->setAttributeBuffer("a_position",GL_FLOAT, 0, 2);

    glLineWidth(10);
    glDrawArrays(GL_LINE_STRIP, 0, 2);

    m_programMovingBar->disableAttributeArray("a_position");
    movingBarVBO.release();
    m_programMovingBar->release();
}

void TraceDisplayRenderer::initTraces()
{

}
void TraceDisplayRenderer::updateTraces()
{

    m_programTraces->bind();

    // ------ These will be moved to mouse and keyboard slots
    m_programTraces->setUniformValueArray("u_pan", pan, 1, 2);
    m_programTraces->setUniformValueArray("u_scale", scale, 1, 2);
    m_programTraces->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programTraces->setUniformValue("u_windowSize", windowSize);
    // ----------------------------------------------------------

    // WILL UPDATE VBO HERE IF NEEDED

    m_programTraces->release();
}

void TraceDisplayRenderer::drawTraces()
{
    QVector<float> tempTime;
    int arrayOffset;
    m_programTraces->bind();

    m_programTraces->setUniformValue("u_time", (float)(currentTime - startTime)/1000.0f);

    for (int num = 0; num < traces.length(); num++) {
        if (num == 0) {
            // Test trace
            traces[num].numDataInBuffer[0] = 10;
            traces[num].numDataInBuffer[1] = 10;
            for (int a=0; a < 10; a++) {
                dataY[0][a] = (float)a/5.0 - 1.0;
                dataY[1][a] = (float)a/5.0 - 1.0;
                dataT[0][a] = currentTime - (float)a/5.0 * 1000;
                dataT[1][a] = currentTime - (float)a/5.0 * 1000;
            }
        }
        if (traces[num].numDataInBuffer[*traces[num].displayBufferNumber] > 0) {
            // Switches the display buffer number and reset data count
            if (*traces[num].displayBufferNumber == 0) {
                    traces[num].numDataInBuffer[0] = 1;
                    traces[num].dataT[0] = traces[num].dataT[traces[num].bufferSize + traces[num].numDataInBuffer[1]];
                    traces[num].dataY[0] = traces[num].dataY[traces[num].bufferSize + traces[num].numDataInBuffer[1]];
                    *traces[num].displayBufferNumber = 1;
                    arrayOffset = traces[num].bufferSize;
            }
            else {
                    traces[num].numDataInBuffer[1] = 1;
                    traces[num].dataT[traces[num].bufferSize] = traces[num].dataT[traces[num].numDataInBuffer[1]];
                    traces[num].dataY[traces[num].bufferSize] = traces[num].dataY[traces[num].numDataInBuffer[1]];
                    *traces[num].displayBufferNumber = 0;
                    arrayOffset = 0;
            }

            // Update uniforms for specific trace
            m_programTraces->setUniformValueArray("u_color", traces[num].color, 1, 3);
            m_programTraces->setUniformValue("u_scaleTrace", traces[num].scale);
            m_programTraces->setUniformValue("u_offset", traces[num].offset + (float)(num * 0.5f - 1.0f));
            m_programTraces->setUniformValue("u_traceSelected", 0.0f);

            // Set the data for the trace
            m_programTraces->enableAttributeArray("a_dataTime");
            m_programTraces->enableAttributeArray("a_dataY");

            tempTime.clear();
            for (int idx=0; idx < traces[num].numDataInBuffer[*traces[num].displayBufferNumber]; idx++) {
                tempTime.append((traces[num].dataT[arrayOffset + idx] - startTime)/1000.0f);
            }
            m_programTraces->setAttributeArray("a_dataTime", GL_FLOAT, &tempTime[0], 1);
            m_programTraces->setAttributeArray("a_dataY", GL_FLOAT, &traces[num].dataY[arrayOffset], 1);


            glLineWidth(10);
            glDrawArrays(GL_LINE_STRIP, 0, traces[num].numDataInBuffer[*traces[num].displayBufferNumber]);
            qDebug() << traces[num].numDataInBuffer[*traces[num].displayBufferNumber];

            m_programTraces->disableAttributeArray("a_dataTime");
            m_programTraces->disableAttributeArray("a_dataY");
        }
    }

    m_programTraces->release();

}

void TraceDisplayRenderer::paint()
{
    currentTime =  QDateTime().currentMSecsSinceEpoch();
    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());

    glDisable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    // Draw vertical grid lines
    updateGridV();
    drawGridV();

    // Draw vertical grid lines
    updateGridH();
    drawGridH();

    // Draw moving vertical bar
    updateMovingBar();
    drawMovingBar();

    // Draws traces
    updateTraces();
    drawTraces();

//    // Not strictly needed for this example, but generally useful for when
//    // mixing with raw OpenGL.
    m_window->resetOpenGLState();

}
