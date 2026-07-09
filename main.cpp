#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDateTime>
#include <QTimer>
#include <QtMath>

class SampleFeeder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double startTime READ startTime NOTIFY startTimeChanged)

public:
    explicit SampleFeeder(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_timer.setInterval(1);
        m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer, &QTimer::timeout, this, &SampleFeeder::generateSample);
        reset();
    }

    double startTime() const { return m_startTime; }

    Q_INVOKABLE void start()
    {
        if (!m_timer.isActive())
            m_timer.start();
    }

    Q_INVOKABLE void stop()
    {
        m_timer.stop();
    }

    Q_INVOKABLE void reset()
    {
        m_startTime = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        m_currentTime = m_startTime;
        emit startTimeChanged();
    }

signals:
    void startTimeChanged();
    void sampleGenerated(double x, double y0, double y1, double y2, double y3, double y4);

private:
    void generateSample()
    {
        m_currentTime = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        const double t = m_currentTime - m_startTime;
        emit sampleGenerated(m_currentTime,
                             qSin(t * 2.6) * 0.95,
                             qCos(t * 1.7) * 0.70,
                             qSin(t * 3.2 + 0.8) * 0.48,
                             qCos(t * 2.4 + 1.4) * 0.36 - 0.55,
                             qSin(t * 1.2 + 2.1) * 0.32 + 0.85);
    }

    QTimer m_timer;
    double m_startTime = 0.0;
    double m_currentTime = 0.0;
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    SampleFeeder sampleFeeder;
    engine.rootContext()->setContextProperty("sampleFeeder", &sampleFeeder);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("QtRhiPlotQML", "Main");

    return app.exec();
}

#include "main.moc"
