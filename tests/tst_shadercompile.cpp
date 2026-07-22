#include <QtTest>

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QFile>

// Compile + link every custom GLSL program the renderers use
// (videodisplay.cpp, tracedisplay.cpp, behaviortracker.cpp), in an offscreen
// context matching what Qt Quick's OpenGL RHI backend creates. The shaders are
// legacy GLSL 1.10 (attribute/varying), which needs a compatibility context -
// notably on macOS, where GL is either 2.1 legacy or 3.2+ core-only, this
// pins the requirement that the default (2.1 legacy) context keeps working.
class TestShaderCompile : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void compileAndLink_data();
    void compileAndLink();

private:
    static QString slurp(const QString &name);
    QOpenGLContext *m_ctx = nullptr;
    QOffscreenSurface *m_surf = nullptr;
};

QString TestShaderCompile::slurp(const QString &name)
{
    QFile f(QStringLiteral(SHADER_SOURCE_DIR "/") + name);
    if (!f.open(QFile::ReadOnly | QFile::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

void TestShaderCompile::initTestCase()
{
    // Same default-format request the app makes: Qt asks for GL 2.x and the
    // platform answers with a compatibility context (2.1 legacy on macOS).
    m_ctx = new QOpenGLContext(this);
    if (!m_ctx->create())
        QSKIP("No OpenGL context available on this machine/runner");
    m_surf = new QOffscreenSurface(nullptr, this);
    m_surf->setFormat(m_ctx->format());
    m_surf->create();
    if (!m_surf->isValid() || !m_ctx->makeCurrent(m_surf))
        QSKIP("Could not make an offscreen GL surface current");
    if (m_ctx->format().majorVersion() < 2)
        QSKIP("GL context < 2.0 (e.g. Windows software GL 1.1) cannot compile GLSL");
    qInfo().nospace() << "GL context: " << m_ctx->format().majorVersion() << "."
                      << m_ctx->format().minorVersion()
                      << " profile=" << m_ctx->format().profile();
}

void TestShaderCompile::compileAndLink_data()
{
    QTest::addColumn<QString>("vert");
    QTest::addColumn<QString>("frag");

    // Pairs exactly as composed by the renderers.
    QTest::newRow("videodisplay")     << "imageBasic.vert"     << "imageSaturationScaling.frag";
    QTest::newRow("trace-texture")    << "texture.vert"        << "texture.frag";
    QTest::newRow("trace-grid")       << "grid.vert"           << "grid.frag";
    QTest::newRow("trace-movingBar")  << "movingBar.vert"      << "movingBar.frag";
    QTest::newRow("trace-traces")     << "trace.vert"          << "trace.frag";
    QTest::newRow("tracker")          << "tracker.vert"        << "tracker.frag";
    QTest::newRow("tracker-overlay")  << "trackerOverlay.vert" << "trackerOverlay.frag";
}

void TestShaderCompile::compileAndLink()
{
    QFETCH(QString, vert);
    QFETCH(QString, frag);

    QVERIFY(m_ctx->makeCurrent(m_surf));

    const QString vsrc = slurp(vert);
    const QString fsrc = slurp(frag);
    QVERIFY2(!vsrc.isEmpty(), qPrintable("missing shader source: " + vert));
    QVERIFY2(!fsrc.isEmpty(), qPrintable("missing shader source: " + frag));

    QOpenGLShaderProgram prog;
    QVERIFY2(prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc),
             qPrintable(vert + ": " + prog.log()));
    QVERIFY2(prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc),
             qPrintable(frag + ": " + prog.log()));
    QVERIFY2(prog.link(), qPrintable(vert + " + " + frag + ": " + prog.log()));
}

QTEST_MAIN(TestShaderCompile)
#include "tst_shadercompile.moc"
