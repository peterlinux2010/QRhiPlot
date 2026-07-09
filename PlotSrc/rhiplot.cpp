#include "rhiplot.h"
#include <QtQuick/QQuickWindow>
#include <QtQuick/QQuickPaintedItem>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRunnable>
#include <QtCore/QStandardPaths>
#include <QtCore/QTextStream>
#include <QtCore/QtMath>
#include <QtCore/QUrl>
#include <QDateTime>
#include <QDebug>
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QHoverEvent>

#include <algorithm>

#include <rhi/qrhi.h>

namespace {

constexpr int kXAxisTickCount = 8;
constexpr int kYAxisTickCount = 6;
constexpr qreal kMinXRange = 0.001; // seconds, keeps timestamp axes above double precision traps
constexpr qreal kMinYRange = 1e-9;

static qreal xTickStep(qreal xMin, qreal xMax)
{
    const qreal range = xMax - xMin;
    return qFuzzyIsNull(range) ? 1.0 : range / kXAxisTickCount;
}

static qreal firstAlignedTick(qreal minValue, qreal step)
{
    if (qFuzzyIsNull(step))
        return minValue;
    return qCeil(minValue / step) * step;
}

static QVector<qreal> xTickValues(qreal xMin, qreal xMax)
{
    QVector<qreal> values;
    const qreal step = xTickStep(xMin, xMax);
    if (!qIsFinite(step) || step <= 0.0)
        return values;

    const qreal firstTick = firstAlignedTick(xMin, step);
    values.reserve(kXAxisTickCount + 3);
    for (int i = 0; i < kXAxisTickCount + 3; ++i) {
        const qreal value = firstTick + step * i;
        if (value > xMax + step * 0.001)
            break;
        if (value >= xMin - step * 0.001)
            values.append(value);
    }
    return values;
}

struct PlotVertex
{
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

struct alignas(16) PlotUniforms
{
    float axisRange[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
    float plotRect[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    float yDir = 1.0f;
    float padding[3] = {};
};

static QVector<float> dashPattern(LineStyle style)
{
    switch (style) {
    case LineStyle::DashLine:
        return { 10.0f, 6.0f };
    case LineStyle::DotLine:
        return { 2.0f, 6.0f };
    case LineStyle::DashDotLine:
        return { 10.0f, 5.0f, 2.0f, 5.0f };
    case LineStyle::DashDotDotLine:
        return { 10.0f, 5.0f, 2.0f, 5.0f, 2.0f, 5.0f };
    case LineStyle::SolidLine:
    case LineStyle::NoLine:
        break;
    }
    return {};
}

static QString csvEscaped(QString value)
{
    if (value.contains('"'))
        value.replace('"', "\"\"");
    if (value.contains(',') || value.contains('"') || value.contains('\n') || value.contains('\r'))
        value = '"' + value + '"';
    return value;
}

static QString defaultExportDirectory()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (path.isEmpty())
        path = QDir::homePath();
    return path;
}

static QString resolvedExportDirectory(QString directoryPath)
{
    directoryPath = directoryPath.trimmed();
    if (directoryPath.isEmpty())
        directoryPath = defaultExportDirectory();

    if (directoryPath == "~") {
        directoryPath = QDir::homePath();
    } else if (directoryPath.startsWith("~/") || directoryPath.startsWith("~\\")) {
        directoryPath = QDir::homePath() + directoryPath.mid(1);
    }

    QDir dir(directoryPath);
    if (dir.isRelative())
        dir = QDir(QDir::home().filePath(directoryPath));
    return QDir::cleanPath(dir.absolutePath());
}

}

class PlotAxisOverlay : public QQuickPaintedItem
{
public:
    explicit PlotAxisOverlay(QQuickItem *parent = nullptr)
        : QQuickPaintedItem(parent)
    {
        setAntialiasing(true);
    }

    void setState(qreal xMin, qreal xMax, qreal yMin, qreal yMax,
                  qreal leftMargin, qreal rightMargin, qreal topMargin, qreal bottomMargin,
                  const QString &xTitle, const QString &yTitle,
                  const QColor &textColor, bool axisVisible,
                  RhiPlot::XAxisLabelMode xAxisLabelMode,
                  bool hoverPopupEnabled, bool hoverInfoValid,
                  qreal hoverX, const QPointF &hoverPosition,
                  const QVariantList &hoverValues)
    {
        m_xMin = xMin;
        m_xMax = xMax;
        m_yMin = yMin;
        m_yMax = yMax;
        m_leftMargin = leftMargin;
        m_rightMargin = rightMargin;
        m_topMargin = topMargin;
        m_bottomMargin = bottomMargin;
        m_xTitle = xTitle;
        m_yTitle = yTitle;
        m_textColor = textColor;
        m_axisVisible = axisVisible;
        m_xAxisLabelMode = xAxisLabelMode;
        m_hoverPopupEnabled = hoverPopupEnabled;
        m_hoverInfoValid = hoverInfoValid;
        m_hoverX = hoverX;
        m_hoverPosition = hoverPosition;
        m_hoverValues = hoverValues;
        update();
    }

    void paint(QPainter *painter) override
    {
        if (!m_axisVisible)
            return;

        const QRectF plotRect(m_leftMargin,
                              m_topMargin,
                              qMax<qreal>(1.0, width() - m_leftMargin - m_rightMargin),
                              qMax<qreal>(1.0, height() - m_topMargin - m_bottomMargin));

        painter->setRenderHint(QPainter::TextAntialiasing, true);
        painter->setPen(m_textColor);

        QFont tickFont = painter->font();
        tickFont.setPixelSize(11);
        painter->setFont(tickFont);

        const qreal xLabelWidth = m_xAxisLabelMode == RhiPlot::TimeXAxis ? 96.0 : 56.0;
        for (qreal value : xTickValues(m_xMin, m_xMax)) {
            const qreal t = (value - m_xMin) / (m_xMax - m_xMin);
            const qreal x = plotRect.left() + plotRect.width() * t;
            painter->drawText(QRectF(x - xLabelWidth / 2.0, plotRect.bottom() + 7.0, xLabelWidth, 16.0),
                              Qt::AlignHCenter | Qt::AlignTop,
                              formatXValue(value));
        }

        for (int i = 0; i <= kYAxisTickCount; ++i) {
            const qreal t = qreal(i) / kYAxisTickCount;
            const qreal y = plotRect.bottom() - plotRect.height() * t;
            const qreal value = m_yMin + (m_yMax - m_yMin) * t;
            painter->drawText(QRectF(2.0, y - 8.0, m_leftMargin - 10.0, 16.0),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QString::number(value, 'f', 1));
        }

        QFont titleFont = painter->font();
        titleFont.setPixelSize(12);
        titleFont.setBold(true);
        painter->setFont(titleFont);

        if (!m_xTitle.isEmpty()) {
            painter->drawText(QRectF(m_leftMargin, height() - 21.0, plotRect.width(), 18.0),
                              Qt::AlignHCenter | Qt::AlignVCenter,
                              m_xTitle);
        }

        if (!m_yTitle.isEmpty()) {
            painter->save();
            painter->translate(16.0, m_topMargin + plotRect.height() / 2.0);
            painter->rotate(-90.0);
            painter->drawText(QRectF(-plotRect.height() / 2.0, -8.0, plotRect.height(), 16.0),
                              Qt::AlignHCenter | Qt::AlignVCenter,
                              m_yTitle);
            painter->restore();
        }

        drawHoverPopup(painter);
    }

private:
    void drawHoverPopup(QPainter *painter)
    {
        if (!m_hoverPopupEnabled || !m_hoverInfoValid)
            return;

        QFont headerFont = painter->font();
        headerFont.setPixelSize(12);
        headerFont.setBold(true);
        QFont valueFont = painter->font();
        valueFont.setPixelSize(11);

        const int rowHeight = 16;
        const qreal panelWidth = 220.0;
        const qreal panelHeight = 14.0 + rowHeight * (m_hoverValues.size() + 1);
        qreal panelX = m_hoverPosition.x() + 14.0;
        qreal panelY = m_hoverPosition.y() + 14.0;
        panelX = qBound<qreal>(6.0, panelX, qMax<qreal>(6.0, width() - panelWidth - 6.0));
        panelY = qBound<qreal>(6.0, panelY, qMax<qreal>(6.0, height() - panelHeight - 6.0));
        const QRectF panelRect(panelX, panelY, panelWidth, panelHeight);

        painter->save();
        painter->setPen(QPen(QColor(32, 36, 42), 1.0));
        painter->setBrush(QColor(249, 251, 253, 235));
        painter->drawRoundedRect(panelRect, 4.0, 4.0);

        qreal textY = panelRect.top() + 7.0;
        painter->setFont(headerFont);
        painter->setPen(QColor(32, 36, 42));
        painter->drawText(QRectF(panelRect.left() + 8.0, textY, panelRect.width() - 16.0, rowHeight),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("x: %1").arg(formatXValue(m_hoverX)));
        textY += rowHeight;

        painter->setFont(valueFont);
        for (const QVariant &entry : m_hoverValues) {
            const QVariantMap item = entry.toMap();
            const QString name = item.value(QStringLiteral("name")).toString();
            const bool valid = item.value(QStringLiteral("valid")).toBool();
            const QColor color = item.value(QStringLiteral("color")).value<QColor>();
            const QString valueText = valid
                    ? QStringLiteral("%1: %2").arg(name, QString::number(item.value(QStringLiteral("y")).toReal(), 'f', 4))
                    : QStringLiteral("%1: --").arg(name);

            painter->setPen(color.isValid() ? color : QColor(32, 36, 42));
            painter->drawText(QRectF(panelRect.left() + 8.0, textY, panelRect.width() - 16.0, rowHeight),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              valueText);
            textY += rowHeight;
        }
        painter->restore();
    }

    QString formatXValue(qreal value) const
    {
        if (m_xAxisLabelMode == RhiPlot::TimeXAxis) {
            return QDateTime::fromMSecsSinceEpoch(qRound64(value * 1000.0))
                    .toLocalTime()
                    .toString(QStringLiteral("HH:mm:ss.zzz"));
        }

        return QString::number(value, 'f', 1);
    }

    qreal m_xMin = -1.0;
    qreal m_xMax = 1.0;
    qreal m_yMin = -1.0;
    qreal m_yMax = 1.0;
    qreal m_leftMargin = 56.0;
    qreal m_rightMargin = 16.0;
    qreal m_topMargin = 16.0;
    qreal m_bottomMargin = 44.0;
    QString m_xTitle;
    QString m_yTitle;
    QColor m_textColor = Qt::white;
    bool m_axisVisible = true;
    RhiPlot::XAxisLabelMode m_xAxisLabelMode = RhiPlot::NumericXAxis;
    bool m_hoverPopupEnabled = true;
    bool m_hoverInfoValid = false;
    qreal m_hoverX = 0.0;
    QPointF m_hoverPosition;
    QVariantList m_hoverValues;
};

namespace {

static void appendSegment(QVector<PlotVertex> &vertices, const QPointF &p0, const QPointF &p1, const QColor &color, qreal opacity)
{
    const float alpha = float(color.alphaF() * opacity);
    const PlotVertex v0 {
        float(p0.x()), float(p0.y()),
        float(color.redF()), float(color.greenF()), float(color.blueF()), alpha
    };
    const PlotVertex v1 {
        float(p1.x()), float(p1.y()),
        float(color.redF()), float(color.greenF()), float(color.blueF()), alpha
    };
    vertices.append(v0);
    vertices.append(v1);
}

static bool clipTest(qreal p, qreal q, qreal &u1, qreal &u2)
{
    if (qFuzzyIsNull(p))
        return q >= 0.0;

    const qreal r = q / p;
    if (p < 0.0) {
        if (r > u2)
            return false;
        if (r > u1)
            u1 = r;
    } else {
        if (r < u1)
            return false;
        if (r < u2)
            u2 = r;
    }

    return true;
}

static bool clipSegmentToRect(const QPointF &source0, const QPointF &source1,
                              qreal xMin, qreal xMax, qreal yMin, qreal yMax,
                              QPointF &clipped0, QPointF &clipped1)
{
    const QPointF delta = source1 - source0;
    qreal u1 = 0.0;
    qreal u2 = 1.0;

    if (!clipTest(-delta.x(), source0.x() - xMin, u1, u2))
        return false;
    if (!clipTest(delta.x(), xMax - source0.x(), u1, u2))
        return false;
    if (!clipTest(-delta.y(), source0.y() - yMin, u1, u2))
        return false;
    if (!clipTest(delta.y(), yMax - source0.y(), u1, u2))
        return false;

    clipped0 = source0 + delta * u1;
    clipped1 = source0 + delta * u2;
    return clipped0 != clipped1;
}

static QVector<QPointF> minMaxSampleByPixelColumn(const QVector<QPointF> &points,
                                                  int startIndex, int endIndex,
                                                  qreal xMin, qreal xMax,
                                                  int pixelWidth)
{
    QVector<QPointF> sampled;
    if (endIndex <= startIndex || pixelWidth <= 0 || qFuzzyCompare(xMin, xMax))
        return sampled;

    const int visibleCount = endIndex - startIndex;
    if (visibleCount <= pixelWidth * 3) {
        sampled.reserve(visibleCount);
        for (int i = startIndex; i < endIndex; ++i)
            sampled.append(points[i]);
        return sampled;
    }

    sampled.reserve(pixelWidth * 3);
    int currentColumn = -1;
    QPointF firstPoint;
    QPointF minPoint;
    QPointF maxPoint;
    QPointF lastPoint;
    bool hasColumn = false;

    const auto flushColumn = [&]() {
        if (!hasColumn)
            return;

        sampled.append(firstPoint);
        if (minPoint.x() <= maxPoint.x()) {
            if (minPoint != firstPoint && minPoint != lastPoint)
                sampled.append(minPoint);
            if (maxPoint != firstPoint && maxPoint != lastPoint && maxPoint != minPoint)
                sampled.append(maxPoint);
        } else {
            if (maxPoint != firstPoint && maxPoint != lastPoint)
                sampled.append(maxPoint);
            if (minPoint != firstPoint && minPoint != lastPoint && minPoint != maxPoint)
                sampled.append(minPoint);
        }
        if (lastPoint != firstPoint)
            sampled.append(lastPoint);
    };

    for (int i = startIndex; i < endIndex; ++i) {
        const QPointF &point = points[i];
        int column = int((point.x() - xMin) / (xMax - xMin) * pixelWidth);
        column = qBound(0, column, pixelWidth - 1);

        if (!hasColumn || column != currentColumn) {
            flushColumn();
            currentColumn = column;
            firstPoint = minPoint = maxPoint = lastPoint = point;
            hasColumn = true;
            continue;
        }

        if (point.y() < minPoint.y())
            minPoint = point;
        if (point.y() > maxPoint.y())
            maxPoint = point;
        lastPoint = point;
    }

    flushColumn();
    return sampled;
}

}

// PlotRenderer 类：负责实际的渲染操作
class PlotRenderer : public QObject
{
    Q_OBJECT
public:
    // 设置窗口
    void setWindow(QQuickWindow *window) { m_window = window; }
    void setPlotState(QSharedPointer<PlotCurveManager> manager,
                      qreal xMin, qreal xMax, qreal yMin, qreal yMax,
                      bool gridVisible, const QColor &gridColor,
                      bool axisVisible, const QColor &axisColor,
                      bool hoverInfoValid, qreal hoverX);
    void setPlotGeometry(const QSizeF &itemSize,
                         qreal leftMargin, qreal rightMargin,
                         qreal topMargin, qreal bottomMargin);

public slots:
    // 帧开始时的处理
    void frameStart();
    // 主渲染通道记录开始
    void mainPassRecordingStart();

private:
    // 窗口指针
    QQuickWindow *m_window = nullptr;
    QVector<PlotVertex> m_vertices;
    PlotUniforms m_uniforms;
    quint32 m_vertexBufferCapacity = 0;
    int m_plotPixelWidth = 1;
    int m_plotPixelHeight = 1;
    // 顶点着色器
    QShader m_vertexShader;
    // 片段着色器
    QShader m_fragmentShader;
    // 顶点缓冲区
    std::unique_ptr<QRhiBuffer> m_vertexBuffer;
    //  uniform 缓冲区
    std::unique_ptr<QRhiBuffer> m_uniformBuffer;
    // 着色器资源绑定
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    // 图形管线
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
};

//! [init]
// RhiPlot 构造函数：连接窗口变化信号
RhiPlot::RhiPlot()
{
    connect(this, &QQuickItem::windowChanged, this, &RhiPlot::handleWindowChanged);

    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::AllButtons);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(qRound(1000.0 / m_refreshRate));
    connect(&m_refreshTimer, &QTimer::timeout, this, &RhiPlot::flushPendingPlotUpdate);

    m_axisOverlay = new PlotAxisOverlay(this);
    m_axisOverlay->setZ(1.0);

    // 创建默认曲线管理器
    m_curveManager = QSharedPointer<PlotCurveManager>::create();
    connect(m_curveManager.data(), &PlotCurveManager::curvesChanged,
            this, &RhiPlot::handleCurvesChanged);
}

// 处理窗口变化：连接窗口的渲染相关信号
void RhiPlot::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &RhiPlot::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &RhiPlot::cleanup, Qt::DirectConnection);
        win->setColor(m_backgroundColor);
    }
}
//! [init]

void RhiPlot::schedulePlotUpdate()
{
    if (!window())
        return;

    if (m_refreshRate <= 0.0) {
        window()->update();
        return;
    }

    m_plotUpdatePending = true;
    if (!m_refreshTimer.isActive())
        m_refreshTimer.start();
}

void RhiPlot::flushPendingPlotUpdate()
{
    if (!m_plotUpdatePending)
        return;

    m_plotUpdatePending = false;
    if (window())
        window()->update();
}

// 安全释放自定义图形资源的方法是同时连接到 sceneGraphInvalidated() 并实现 releaseResources()
// 为了支持线程化渲染循环，后者通过 scheduleRenderJob() 执行 PlotRenderer 销毁
// 注意：当 QRunnable 被调用时，RhiPlot 可能已经不存在

// 清理资源：在渲染线程上调用
void RhiPlot::cleanup()
{
    delete m_renderer;
    m_renderer = nullptr;
}

// CleanupJob 类：用于在渲染线程上清理 PlotRenderer
class CleanupJob : public QRunnable
{
public:
    CleanupJob(PlotRenderer *renderer) : m_renderer(renderer) { }
    void run() override { delete m_renderer; }
private:
    PlotRenderer *m_renderer;
};

// 释放资源：通过渲染作业清理资源
void RhiPlot::releaseResources()
{
    window()->scheduleRenderJob(new CleanupJob(m_renderer), QQuickWindow::BeforeSynchronizingStage);
    m_renderer = nullptr;
}

void RhiPlot::wheelEvent(QWheelEvent *event)
{
    if (!m_zoomEnabled || qFuzzyCompare(m_xMin, m_xMax) || qFuzzyCompare(m_yMin, m_yMax)) {
        event->ignore();
        return;
    }

    const QPoint angleDelta = event->angleDelta();
    const int wheelDelta = angleDelta.y() != 0 ? angleDelta.y() : angleDelta.x();
    if (wheelDelta == 0) {
        event->ignore();
        return;
    }

    const QPointF pos = event->position();
    const qreal plotLeft = m_plotLeftMargin;
    const qreal plotRight = qMax<qreal>(plotLeft + 1.0, width() - m_plotRightMargin);
    const qreal plotTop = m_plotTopMargin;
    const qreal plotBottom = qMax<qreal>(plotTop + 1.0, height() - m_plotBottomMargin);
    const QRectF plotRect(plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop);

    const bool onXAxis = pos.x() >= plotRect.left() && pos.x() <= plotRect.right()
            && pos.y() > plotRect.bottom() && pos.y() <= height();
    const bool onYAxis = pos.y() >= plotRect.top() && pos.y() <= plotRect.bottom()
            && pos.x() >= 0.0 && pos.x() < plotRect.left();
    const bool zoomX = !m_followLatestX && (onXAxis || (!onYAxis && !onXAxis));
    const bool zoomY = m_followLatestX || onYAxis || (!onYAxis && !onXAxis);

    const qreal zoomFactor = wheelDelta > 0 ? 0.85 : 1.0 / 0.85;
    const qreal xNorm = plotRect.width() > 0.0 ? qBound<qreal>(0.0, (pos.x() - plotRect.left()) / plotRect.width(), 1.0) : 0.5;
    const qreal yNorm = plotRect.height() > 0.0 ? qBound<qreal>(0.0, (plotRect.bottom() - pos.y()) / plotRect.height(), 1.0) : 0.5;

    if (zoomX) {
        const qreal anchor = m_xMin + (m_xMax - m_xMin) * xNorm;
        const qreal newRange = qMax<qreal>((m_xMax - m_xMin) * zoomFactor, kMinXRange);
        m_xMin = anchor - (anchor - m_xMin) * zoomFactor;
        m_xMax = m_xMin + newRange;
        emit xRangeChanged();
    }

    if (zoomY) {
        const qreal anchor = m_yMin + (m_yMax - m_yMin) * yNorm;
        const qreal newRange = qMax<qreal>((m_yMax - m_yMin) * zoomFactor, kMinYRange);
        m_yMin = anchor - (anchor - m_yMin) * zoomFactor;
        m_yMax = m_yMin + newRange;
        emit yRangeChanged();
    }

    if (m_autoRange) {
        m_autoRange = false;
        emit autoRangeChanged();
    }

    event->accept();
    updateHoverInfo(pos);
    schedulePlotUpdate();
}

void RhiPlot::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    const QPointF pos = event->position();
    const qreal plotLeft = m_plotLeftMargin;
    const qreal plotRight = qMax<qreal>(plotLeft + 1.0, width() - m_plotRightMargin);
    const qreal plotTop = m_plotTopMargin;
    const qreal plotBottom = qMax<qreal>(plotTop + 1.0, height() - m_plotBottomMargin);
    const QRectF plotRect(plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop);

    const bool onXAxis = pos.x() >= plotRect.left() && pos.x() <= plotRect.right()
            && pos.y() > plotRect.bottom() && pos.y() <= height();
    const bool onYAxis = pos.y() >= plotRect.top() && pos.y() <= plotRect.bottom()
            && pos.x() >= 0.0 && pos.x() < plotRect.left();

    m_dragX = !m_followLatestX && (onXAxis || (!onXAxis && !onYAxis));
    m_dragY = m_followLatestX || onYAxis || (!onXAxis && !onYAxis);
    m_dragging = m_dragX || m_dragY;
    m_lastMousePos = pos;

    event->setAccepted(m_dragging);
}

void RhiPlot::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || qFuzzyCompare(m_xMin, m_xMax) || qFuzzyCompare(m_yMin, m_yMax)) {
        event->ignore();
        return;
    }

    const QPointF pos = event->position();
    const QPointF delta = pos - m_lastMousePos;
    m_lastMousePos = pos;

    const qreal plotWidth = qMax<qreal>(1.0, width() - m_plotLeftMargin - m_plotRightMargin);
    const qreal plotHeight = qMax<qreal>(1.0, height() - m_plotTopMargin - m_plotBottomMargin);

    if (m_dragX) {
        const qreal dataDelta = -delta.x() / plotWidth * (m_xMax - m_xMin);
        m_xMin += dataDelta;
        m_xMax += dataDelta;
        emit xRangeChanged();
    }

    if (m_dragY) {
        const qreal dataDelta = delta.y() / plotHeight * (m_yMax - m_yMin);
        m_yMin += dataDelta;
        m_yMax += dataDelta;
        emit yRangeChanged();
    }

    if (m_autoRange) {
        m_autoRange = false;
        emit autoRangeChanged();
    }

    event->accept();
    schedulePlotUpdate();
}

void RhiPlot::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        m_dragX = false;
        m_dragY = false;
        event->accept();
        return;
    }

    event->ignore();
}

void RhiPlot::hoverMoveEvent(QHoverEvent *event)
{
    updateHoverInfo(event->position());
    event->accept();
}

void RhiPlot::hoverLeaveEvent(QHoverEvent *event)
{
    Q_UNUSED(event);
    clearHoverInfo();
}

qreal RhiPlot::xValueAtPosition(qreal positionX) const
{
    const qreal plotLeft = m_plotLeftMargin;
    const qreal plotWidth = qMax<qreal>(1.0, width() - m_plotLeftMargin - m_plotRightMargin);
    const qreal t = qBound<qreal>(0.0, (positionX - plotLeft) / plotWidth, 1.0);
    return m_xMin + (m_xMax - m_xMin) * t;
}

void RhiPlot::updateHoverInfo(const QPointF &position)
{
    const QRectF plotRect(m_plotLeftMargin,
                          m_plotTopMargin,
                          qMax<qreal>(1.0, width() - m_plotLeftMargin - m_plotRightMargin),
                          qMax<qreal>(1.0, height() - m_plotTopMargin - m_plotBottomMargin));

    if (m_followLatestX || !plotRect.contains(position) || !m_curveManager) {
        clearHoverInfo();
        return;
    }

    const qreal xValue = xValueAtPosition(position.x());
    QVariantList values;

    for (const auto &curve : m_curveManager->curves()) {
        QVariantMap item;
        if (!curve || curve->isEmpty() || !curve->appearance() || !curve->appearance()->visible()) {
            item.insert(QStringLiteral("valid"), false);
            values.append(item);
            continue;
        }

        item.insert(QStringLiteral("name"), curve->name());
        item.insert(QStringLiteral("color"), curve->appearance()->lineColor());

        const QVector<QPointF> &points = curve->dataRef();
        if (xValue < points.constFirst().x() || xValue > points.constLast().x()) {
            item.insert(QStringLiteral("valid"), false);
            values.append(item);
            continue;
        }

        const int rightIndex = curve->lowerBoundX(xValue);
        if (rightIndex == 0) {
            item.insert(QStringLiteral("valid"), true);
            item.insert(QStringLiteral("x"), points.constFirst().x());
            item.insert(QStringLiteral("y"), points.constFirst().y());
            values.append(item);
            continue;
        }
        if (rightIndex >= points.size()) {
            item.insert(QStringLiteral("valid"), true);
            item.insert(QStringLiteral("x"), points.constLast().x());
            item.insert(QStringLiteral("y"), points.constLast().y());
            values.append(item);
            continue;
        }

        const QPointF &left = points[rightIndex - 1];
        const QPointF &right = points[rightIndex];
        if (qFuzzyCompare(left.x(), right.x())) {
            item.insert(QStringLiteral("valid"), false);
            values.append(item);
            continue;
        }

        const qreal t = (xValue - left.x()) / (right.x() - left.x());
        const qreal yValue = left.y() + (right.y() - left.y()) * t;
        item.insert(QStringLiteral("valid"), true);
        item.insert(QStringLiteral("x"), xValue);
        item.insert(QStringLiteral("y"), yValue);
        values.append(item);
    }

    m_hoverInfoValid = true;
    m_hoverX = xValue;
    m_hoverPosition = position;
    m_hoverValues = values;
    emit hoverInfoChanged();
    schedulePlotUpdate();
}

void RhiPlot::clearHoverInfo()
{
    if (!m_hoverInfoValid && m_hoverValues.isEmpty())
        return;

    m_hoverInfoValid = false;
    m_hoverValues.clear();
    emit hoverInfoChanged();
    schedulePlotUpdate();
}

//! [sync]
// 同步操作：创建渲染器并设置连接
void RhiPlot::sync()
{
    if (!m_renderer) {
        m_renderer = new PlotRenderer;
        // 初始化资源在开始记录渲染通道之前完成
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &PlotRenderer::frameStart, Qt::DirectConnection);
        // 这里使用底层渲染，因此连接到 beforeRenderPassRecording
        // 改为 afterRenderPassRecording 会将图形渲染在顶部（覆盖层）
        connect(window(), &QQuickWindow::beforeRenderPassRecording, m_renderer, &PlotRenderer::mainPassRecordingStart, Qt::DirectConnection);
    }
    m_renderer->setWindow(window());
    m_renderer->setPlotGeometry(QSizeF(width(), height()),
                                m_plotLeftMargin, m_plotRightMargin,
                                m_plotTopMargin, m_plotBottomMargin);
    m_renderer->setPlotState(m_curveManager,
                             m_xMin, m_xMax, m_yMin, m_yMax,
                             m_gridVisible, m_gridColor,
                             m_axisVisible, m_axisColor,
                             m_hoverInfoValid, m_hoverX);

    if (m_axisOverlay) {
        m_axisOverlay->setSize(QSizeF(width(), height()));
        m_axisOverlay->setState(m_xMin, m_xMax, m_yMin, m_yMax,
                                m_plotLeftMargin, m_plotRightMargin,
                                m_plotTopMargin, m_plotBottomMargin,
                                m_xAxisTitle, m_yAxisTitle,
                                m_axisTextColor, m_axisVisible,
                                m_xAxisLabelMode,
                                m_hoverPopupEnabled, m_hoverInfoValid,
                                m_hoverX, m_hoverPosition,
                                m_hoverValues);
    }
}
//! [sync]

// 获取着色器：从文件加载序列化的着色器
static QShader getShader(const QString &name)
{
    QFile f(name);
    if (f.open(QIODevice::ReadOnly))
        return QShader::fromSerialized(f.readAll());

    return QShader();
}

void PlotRenderer::setPlotGeometry(const QSizeF &itemSize,
                                   qreal leftMargin, qreal rightMargin,
                                   qreal topMargin, qreal bottomMargin)
{
    const qreal w = qMax<qreal>(1.0, itemSize.width());
    const qreal h = qMax<qreal>(1.0, itemSize.height());
    const qreal left = qBound<qreal>(0.0, leftMargin, w - 1.0);
    const qreal right = qBound<qreal>(left + 1.0, w - rightMargin, w);
    const qreal top = qBound<qreal>(0.0, topMargin, h - 1.0);
    const qreal bottom = qBound<qreal>(top + 1.0, h - bottomMargin, h);
    const qreal devicePixelRatio = m_window ? m_window->effectiveDevicePixelRatio() : 1.0;
    m_plotPixelWidth = qMax(1, int((right - left) * devicePixelRatio));
    m_plotPixelHeight = qMax(1, int((bottom - top) * devicePixelRatio));

    m_uniforms.plotRect[0] = float(left / w * 2.0 - 1.0);
    m_uniforms.plotRect[1] = float(1.0 - bottom / h * 2.0);
    m_uniforms.plotRect[2] = float(right / w * 2.0 - 1.0);
    m_uniforms.plotRect[3] = float(1.0 - top / h * 2.0);
}

void PlotRenderer::setPlotState(QSharedPointer<PlotCurveManager> manager,
                                qreal xMin, qreal xMax, qreal yMin, qreal yMax,
                                bool gridVisible, const QColor &gridColor,
                                bool axisVisible, const QColor &axisColor,
                                bool hoverInfoValid, qreal hoverX)
{
    m_vertices.clear();

    if (qFuzzyCompare(xMin, xMax) || qFuzzyCompare(yMin, yMax))
        return;

    const qreal xRange = xMax - xMin;
    const qreal yRange = yMax - yMin;
    m_uniforms.axisRange[0] = 0.0f;
    m_uniforms.axisRange[1] = float(xRange);
    m_uniforms.axisRange[2] = float(yMin);
    m_uniforms.axisRange[3] = float(yMax);

    const auto appendVisibleSegment = [this, xMin](const QPointF &p0, const QPointF &p1, const QColor &color, qreal opacity) {
        appendSegment(m_vertices,
                      QPointF(p0.x() - xMin, p0.y()),
                      QPointF(p1.x() - xMin, p1.y()),
                      color,
                      opacity);
    };

    if (gridVisible) {
        m_vertices.reserve(m_vertices.size() + (kXAxisTickCount + 1) * 2 + (kYAxisTickCount + 1) * 2);

        for (qreal x : xTickValues(xMin, xMax))
            appendVisibleSegment(QPointF(x, yMin), QPointF(x, yMax), gridColor, 1.0);

        for (int i = 0; i <= kYAxisTickCount; ++i) {
            const qreal y = yMin + yRange * i / kYAxisTickCount;
            appendVisibleSegment(QPointF(xMin, y), QPointF(xMax, y), gridColor, 1.0);
        }
    }

    if (axisVisible) {
        appendVisibleSegment(QPointF(xMin, yMin), QPointF(xMin, yMax), axisColor, 1.0);
        appendVisibleSegment(QPointF(xMin, yMin), QPointF(xMax, yMin), axisColor, 1.0);

        const qreal xTickLength = xRange * 0.012;
        const qreal yTickLength = yRange * 0.018;

        for (qreal x : xTickValues(xMin, xMax))
            appendVisibleSegment(QPointF(x, yMin), QPointF(x, yMin + yTickLength), axisColor, 1.0);

        for (int i = 0; i <= kYAxisTickCount; ++i) {
            const qreal y = yMin + yRange * i / kYAxisTickCount;
            appendVisibleSegment(QPointF(xMin, y), QPointF(xMin + xTickLength, y), axisColor, 1.0);
        }

        if (xMin < 0.0 && xMax > 0.0)
            appendVisibleSegment(QPointF(0.0, yMin), QPointF(0.0, yMax), axisColor, 0.45);
        if (yMin < 0.0 && yMax > 0.0)
            appendVisibleSegment(QPointF(xMin, 0.0), QPointF(xMax, 0.0), axisColor, 0.45);
    }

    if (hoverInfoValid && hoverX >= xMin && hoverX <= xMax) {
        QColor hoverLineColor = axisColor;
        hoverLineColor.setAlphaF(qMin(1.0, axisColor.alphaF() * 0.75));

        const qreal yPerPixel = yRange / qMax(1, m_plotPixelHeight);
        const QVector<qreal> pattern = { 5.0 * yPerPixel, 4.0 * yPerPixel };
        int patternIndex = 0;
        qreal patternRemaining = pattern[0];
        bool drawSegment = true;
        qreal consumed = 0.0;
        while (consumed < yRange) {
            const qreal step = qMin<qreal>(patternRemaining * yRange, yRange - consumed);
            if (drawSegment) {
                appendVisibleSegment(QPointF(hoverX, yMin + consumed),
                                     QPointF(hoverX, yMin + consumed + step),
                                     hoverLineColor,
                                     hoverLineColor.alphaF());
            }

            consumed += step;
            patternIndex = (patternIndex + 1) % pattern.size();
            patternRemaining = pattern[patternIndex];
            drawSegment = (patternIndex % 2) == 0;
        }
    }

    if (!manager)
        return;

    for (const auto &curve : manager->curves()) {
        if (!curve || curve->pointCount() < 2 || !curve->appearance())
            continue;

        const auto appearance = curve->appearance();
        if (!appearance->visible() || appearance->lineStyle() == LineStyle::NoLine)
            continue;

        const QVector<QPointF> &points = curve->dataRef();
        if (points.size() < 2)
            continue;

        int startIndex = curve->lowerBoundX(xMin);
        int endIndex = curve->upperBoundX(xMax);
        startIndex = qMax(0, startIndex - 1);
        endIndex = qMin(points.size(), endIndex + 1);
        if (endIndex - startIndex < 2)
            continue;

        const QColor color = appearance->lineColor();
        const qreal opacity = appearance->opacity();
        const QVector<float> pattern = dashPattern(appearance->lineStyle());

        if (pattern.isEmpty()) {
            const QVector<QPointF> sampledPoints = minMaxSampleByPixelColumn(points,
                                                                              startIndex,
                                                                              endIndex,
                                                                              xMin,
                                                                              xMax,
                                                                              m_plotPixelWidth);
            m_vertices.reserve(m_vertices.size() + qMax(0, sampledPoints.size() - 1) * 2);
            for (int i = 1; i < sampledPoints.size(); ++i) {
                QPointF p0;
                QPointF p1;
                if (clipSegmentToRect(sampledPoints[i - 1], sampledPoints[i], xMin, xMax, yMin, yMax, p0, p1))
                    appendVisibleSegment(p0, p1, color, opacity);
            }
            continue;
        }

        int patternIndex = 0;
        float patternRemaining = pattern[0];
        bool drawSegment = true;

        for (int i = startIndex + 1; i < endIndex; ++i) {
            QPointF p0;
            QPointF p1;
            if (!clipSegmentToRect(points[i - 1], points[i], xMin, xMax, yMin, yMax, p0, p1))
                continue;

            const QPointF delta = p1 - p0;
            const float segmentLength = float(qSqrt(qPow(delta.x() / xRange, 2.0) + qPow(delta.y() / yRange, 2.0)) * 1000.0);
            if (segmentLength <= 0.0f)
                continue;

            float consumed = 0.0f;
            while (consumed < segmentLength) {
                const float step = qMin(patternRemaining, segmentLength - consumed);
                if (drawSegment) {
                    const qreal t0 = consumed / segmentLength;
                    const qreal t1 = (consumed + step) / segmentLength;
                    appendVisibleSegment(p0 + delta * t0,
                                         p0 + delta * t1,
                                         color,
                                         opacity);
                }

                consumed += step;
                patternRemaining -= step;
                if (patternRemaining <= 0.0001f) {
                    patternIndex = (patternIndex + 1) % pattern.size();
                    patternRemaining = pattern[patternIndex];
                    drawSegment = (patternIndex % 2) == 0;
                }
            }
        }
    }
}

//! [frame-start]
// ========== 帧开始处理：初始化渲染资源 ==========
// ========== 帧开始处理：初始化渲染资源 ==========
// 调用时机：QQuickWindow::beforeRendering 信号触发
// 这个信号在每帧渲染前发出，是进行 RHI 资源准备的最佳时机
//
// 主要任务：
// 1. 获取 RHI 上下文和交换链
// 2. 创建/更新渲染资源（缓冲区、管线等）
// 3. 准备命令缓冲区
//
// 渲染管线资源创建流程：
// RHI 实例 → 交换链 → 顶点缓冲区 → Uniform 缓冲区 → SRB → 管线对象
void PlotRenderer::frameStart()
{
    // ===== 步骤 1：获取 RHI 实例 =====
    // RHI (Render Hardware Interface) 是 Qt 的渲染硬件抽象层
    // 
    // 核心作用：
    // - 统一封装底层图形 API（OpenGL / Vulkan / Direct3D 12）
    // - 提供跨平台的渲染接口，无需关心底层 API 差异
    // - 自动选择最优的图形后端（根据平台和硬件）
    //
    // 为什么需要 RHI？
    // - 不同平台支持不同的图形 API（Windows: D3D/OpenGL, macOS: Metal, Linux: OpenGL/Vulkan）
    // - RHI 提供统一接口，一套代码可在所有平台运行
    // - 避免直接处理各 API 的复杂性和差异
    //
    // m_window->rhi() 返回当前 QQuickWindow 使用的 RHI 实例
    // 所有 RHI 资源（缓冲区、管线、着色器等）都必须通过这个实例创建
    QRhi *rhi = m_window->rhi();
    if (!rhi) {
        qWarning("QQuickWindow is not using QRhi for rendering");
        return;
    }
    
    // ===== 步骤 2：获取交换链 (SwapChain) =====
    // 交换链是渲染到屏幕的核心组件
    //
    // 交换链的职责：
    // 1. 管理帧缓冲区（Framebuffer）：
    //    - 提供当前帧的渲染目标（颜色缓冲区、深度缓冲区）
    //    - 处理多采样抗锯齿（MSAA）
    //
    // 2. 管理命令缓冲区（Command Buffer）：
    //    - 提供录制绘制命令的载体
    //    - 命令缓冲区包含 GPU 执行的所有指令
    //
    // 3. 管理缓冲区交换（Buffer Swap）：
    //    - 使用双缓冲或三缓冲机制
    //    - 前缓冲区：正在显示
    //    - 后缓冲区：正在渲染
    //    - 渲染完成后交换，避免画面撕裂
    //
    // 4. 管理渲染通道（Render Pass）：
    //    - 定义渲染过程中的加载/存储操作
    //    - 优化 tile-based GPU 的性能
    QRhiSwapChain *swapChain = m_window->swapChain();
    if (!swapChain) {
        qWarning("No QRhiSwapChain?");
        return;
    }
    
    // ===== 步骤 3：创建资源更新批次 (Resource Update Batch) =====
    // QRhiResourceUpdateBatch 是资源数据上传的批处理机制
    //
    // 为什么需要批量更新？
    // - CPU 到 GPU 的数据传输（PCIe）开销很大
    // - 频繁的小数据传输会严重降低性能
    // - 批量更新可以合并多次操作，减少传输次数
    //
    // 工作原理：
    // 1. 录制阶段：记录所有资源更新操作（uploadStaticBuffer、updateDynamicBuffer 等）
    // 2. 提交阶段：在合适的时机一次性提交所有更新
    // 3. 异步执行：GPU 在后台处理数据传输，不阻塞 CPU
    //
    // 使用时机：
    // - 每帧开始时获取一个新的批次
    // - 将所有资源更新操作记录到这个批次
    // - 在命令缓冲区提交时自动执行
    QRhiResourceUpdateBatch *resourceUpdates = rhi->nextResourceUpdateBatch();

    // ===== 步骤 4：首次渲染时创建渲染资源（延迟初始化）=====
    // 使用延迟初始化（Lazy Initialization）模式
    // 
    // 为什么使用延迟初始化？
    // - RHI 资源创建开销大（GPU 内存分配、驱动编译等）
    // - 只在真正需要时创建，避免不必要的资源浪费
    // - 后续帧直接复用已创建的资源，提高性能
    //
    // 判断条件：m_pipeline 为空表示首次渲染
    // 资源创建顺序（依赖关系）：
    // 着色器 → 顶点缓冲区 → Uniform 缓冲区 → SRB → 管线对象
    if (!m_pipeline) {
        // --- 4.1 加载着色器 (Shader) ---
        // 着色器是运行在 GPU 上的小程序，控制渲染管线的各个阶段
        //
        // Qt 着色器格式 (.qsb)：
        // - Qt Shader Builder 格式，跨平台编译后的着色器
        // - 支持从 GLSL、HLSL、MSL 编译
        // - 自动适配不同图形后端
        //
        // 顶点着色器 (Vertex Shader)：
        // - 处理每个顶点，执行坐标变换
        // - 输出顶点位置给光栅化器
        //
        // 片段着色器 (Fragment Shader)：
        // - 处理每个像素，计算最终颜色
        // - 可以访问纹理、Uniform 等数据
        m_vertexShader = getShader(QLatin1String(":/scenegraph/rhiplotqml/plot_rhi.vert.qsb"));
        if (!m_vertexShader.isValid())
            qWarning("Failed to load vertex shader; rendering will be incorrect");

        m_fragmentShader = getShader(QLatin1String(":/scenegraph/rhiplotqml/plot_rhi.frag.qsb"));
        if (!m_fragmentShader.isValid())
            qWarning("Failed to load fragment shader; rendering will be incorrect");

        // --- 4.2 创建顶点缓冲区 (Vertex Buffer) ---
        // 顶点缓冲区是 GPU 内存区域，存储顶点数据
        //
        // 顶点数据包含：
        // - 位置坐标（x, y, z）
        // - 颜色（r, g, b, a）
        // - 纹理坐标（u, v）
        // - 法线向量（用于光照计算）
        //
        // 缓冲区类型选择：
        // - Immutable（不可变）：创建后不修改，性能最优
        // - Dynamic（动态）：每帧可更新，适合动画
        // - Static（静态）：偶尔更新
        //
        // 参数说明：
        // - QRhiBuffer::Immutable: 不可变缓冲区
        // - QRhiBuffer::VertexBuffer: 用途是顶点数据
        // - sizeof(vertices): 缓冲区大小（字节）
        m_vertexBufferCapacity = qMax<quint32>(quint32(sizeof(PlotVertex)), quint32(m_vertices.size() * sizeof(PlotVertex)));
        m_vertexBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, m_vertexBufferCapacity));
        m_vertexBuffer->create();  // 在 GPU 上分配内存

        // --- 4.3 创建 Uniform 缓冲区 ---
        // Uniform 缓冲区存储着色器需要的动态参数
        //
        // Uniform vs 顶点属性：
        // - Uniform：所有顶点共享，每帧可能变化（如时间、变换矩阵）
        // - 顶点属性：每个顶点独立，通常不变（如位置、颜色）
        //
        // 为什么使用 Uniform 缓冲区？
        // - 比逐个设置 Uniform 变量高效
        // - 支持批量更新多个 Uniform
        // - 现代 GPU 的标准做法
        //
        // 参数说明：
        // - QRhiBuffer::Dynamic: 动态缓冲区，每帧可能更新
        // - QRhiBuffer::UniformBuffer: 用途是 Uniform 数据
        // - UBUF_SIZE: 大小 = 8 bytes (2 个 float: t 和 y_dir)
        const quint32 UBUF_SIZE = sizeof(PlotUniforms);
        m_uniformBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UBUF_SIZE));
        m_uniformBuffer->create();  // 在 GPU 上分配内存

        // --- 4.4 设置 Y 轴方向 ---
        // 不同图形 API 的 NDC（标准化设备坐标）Y 轴方向不同：
        //
        // NDC 坐标系：
        // - X 轴：-1（左）到 1（右）
        // - Y 轴：方向因 API 而异
        // - Z 轴：深度（0 到 1 或 -1 到 1）
        //
        // Y 轴方向差异：
        // - OpenGL: Y 向上为正（+Y 是上方），NDC Y 范围 [-1, 1]
        // - Direct3D/Vulkan: Y 向下为正（+Y 是下方）
        //
        // 为什么需要适配？
        // - RHI 自动检测当前使用的图形后端
        // - 根据后端设置正确的 Y 方向
        // - 保证跨平台渲染结果一致
        m_uniforms.yDir = rhi->isYUpInNDC() ? 1.0f : -1.0f;
        
        resourceUpdates->updateDynamicBuffer(m_uniformBuffer.get(), 0, sizeof(PlotUniforms), &m_uniforms);

        // --- 4.5 创建着色器资源绑定 (Shader Resource Bindings, SRB) ---
        // SRB 管理着色器访问的资源（Uniform 缓冲区、纹理、存储缓冲区等）
        //
        // 为什么需要 SRB？
        // - GPU 需要知道着色器可以访问哪些资源
        // - SRB 建立资源与着色器的绑定关系
        // - 类似"权限表"：声明哪些资源对哪些着色器阶段可见
        //
        // Binding 机制：
        // - 每个资源有一个 binding 编号（类似数组索引）
        // - 着色器通过 binding 编号访问资源
        // - binding 编号在着色器中声明：layout(binding = 0) uniform ...
        m_srb.reset(rhi->newShaderResourceBindings());
        
        // 定义资源可见性：
        // 指定哪些着色器阶段可以访问这些资源
        // - VertexStage: 顶点着色器
        // - FragmentStage: 片段着色器
        // - ComputeStage: 计算着色器
        const auto visibleToAll = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        
        // 设置绑定配置：
        // uniformBuffer(0, visibleToAll, m_uniformBuffer.get())
        // 参数说明：
        // - 0: binding 编号（对应 shader 中的 layout(binding = 0)）
        // - visibleToAll: 顶点和片段着色器都能访问
        // - m_uniformBuffer.get(): 实际的 Uniform 缓冲区指针
        //
        // setBindings 接受一个绑定列表：
        // 可以同时绑定多个资源（Uniform 缓冲区、纹理等）
        m_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, visibleToAll, m_uniformBuffer.get())
        });
        m_srb->create();  // 创建 SRB 对象

        // --- 4.6 设置顶点输入布局 (Vertex Input Layout) ---
        // 告诉 GPU 如何解析顶点缓冲区中的数据
        //
        // 为什么需要输入布局？
        // - 顶点缓冲区是原始字节数据
        // - GPU 需要知道如何解释这些字节
        // - 例如：每 8 bytes 是一个顶点，包含 2 个 float（x, y）
        //
        // 核心概念：
        // - Binding（绑定）：定义顶点数据的内存布局（步长）
        // - Attribute（属性）：定义单个顶点属性的格式（位置、颜色等）
        QRhiVertexInputLayout inputLayout;
        
        // 设置顶点绑定（Binding）：
        // 定义顶点数据的组织方式
        // - 步长（stride）：每个顶点占用的字节数
        // - 这里：2 * sizeof(float) = 8 bytes
        // - 意思：每个顶点占 8 bytes（x, y 两个 float）
        inputLayout.setBindings({
            { sizeof(PlotVertex) }
        });
        
        // 设置顶点属性（Attribute）：
        // 定义单个属性的格式和位置
        // 参数说明：
        // - location=0: 对应 shader 中的 layout(location = 0) in vec2 aPosition
        // - binding=0: 使用第 0 个绑定（上面的步长定义）
        // - Float2: 数据类型是 2 个 float（vec2）
        // - offset=0: 从顶点开头偏移 0 bytes（从第一个字节开始）
        //
        // 多属性示例（如果有颜色和纹理）：
        // { 0, 0, QRhiVertexInputAttribute::Float2, 0 }  // binding 0, location 0
        // { 0, 1, QRhiVertexInputAttribute::Float4, 8 }  // binding 0, location 1
        // { 0, 2, QRhiVertexInputAttribute::Float2, 24 } // binding 0, location 2
        inputLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
            { 0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float) }
        });

        // --- 4.7 创建图形管线 (Graphics Pipeline) ---
        // 图形管线是渲染的核心配置对象
        // 它定义了从顶点数据到最终像素的完整处理流程
        //
        // 图形管线的处理流程：
        // 1. 顶点着色器（Vertex Shader）：处理每个顶点
        // 2. 图元装配（Primitive Assembly）：连接顶点成线/三角形
        // 3. 光栅化（Rasterization）：将图元转换为像素片段
        // 4. 片段着色器（Fragment Shader）：计算每个像素颜色
        // 5. 混合输出（Blending & Output）：混合到帧缓冲区
        //
        // 管线配置包含：
        // - 拓扑类型（如何连接顶点）
        // - 着色器程序（顶点/片段着色器）
        // - 顶点输入格式（如何解析顶点数据）
        // - 混合模式（透明度处理）
        // - 深度/模板测试（3D 遮挡关系）
        // - 视口和裁剪区域
        m_pipeline.reset(rhi->newGraphicsPipeline());
        
        // 曲线、网格和坐标轴都在 CPU 侧打包成独立线段，每两个顶点绘制一条线。
        m_pipeline->setTopology(QRhiGraphicsPipeline::Lines);
        
        // --- 4.7.1 设置混合模式 (Blending) ---
        // 混合模式决定新绘制的像素（源）如何与已有像素（目标）混合
        //
        // 为什么需要混合？
        // - 实现透明度效果（半透明、渐变）
        // - 叠加多层渲染（UI、特效）
        // - 抗锯齿边缘平滑
        //
        // 混合公式：
        // 最终颜色 = 源颜色 × 源因子 + 目标颜色 × 目标因子
        //
        // 常用混合因子：
        // - Zero: 0
        // - One: 1
        // - SrcAlpha: 源颜色的 Alpha 值
        // - OneMinusSrcAlpha: 1 - 源 Alpha
        // - DstAlpha: 目标颜色的 Alpha 值
        //
        // 常见混合模式：
        // 1. 不透明（覆盖）：
        //    src = One, dst = Zero → 最终 = 源颜色
        //
        // 2. 正常透明（Alpha Blending）：
        //    src = SrcAlpha, dst = OneMinusSrcAlpha
        //    最终 = 源 × Alpha + 目标 × (1 - Alpha)
        //
        // 3.  additive（叠加）：
        //    src = SrcAlpha, dst = One
        //    最终 = 源 × Alpha + 目标
        //    效果：越叠加越亮（适合光效、火焰）
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;  // 启用混合
        
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_pipeline->setTargetBlends({ blend });
        
        // --- 4.7.2 设置着色器阶段 (Shader Stages) ---
        // 指定管线使用的顶点和片段着色器
        //
        // 着色器阶段：
        // - Vertex: 顶点着色器（必需）
        // - Fragment: 片段着色器（必需）
        // - Geometry: 几何着色器（可选）
        // - Tessellation: 曲面细分（可选）
        // - Compute: 计算着色器（独立于图形管线）
        //
        // QRhiShaderStage 结构：
        // - 第一个参数：阶段类型（Vertex/Fragment）
        // - 第二个参数：着色器对象（QRhiShader）
        m_pipeline->setShaderStages({
            { QRhiShaderStage::Vertex, m_vertexShader },
            { QRhiShaderStage::Fragment, m_fragmentShader }
        });
        
        // --- 4.7.3 设置顶点输入布局 (Vertex Input Layout) ---
        // 应用之前定义的顶点输入配置
        // 告诉管线如何从顶点缓冲区读取数据
        m_pipeline->setVertexInputLayout(inputLayout);
        
        // --- 4.7.4 设置资源绑定 (Shader Resource Bindings) ---
        // 应用之前创建的 SRB
        // 这样着色器才能访问 Uniform 缓冲区、纹理等资源
        m_pipeline->setShaderResourceBindings(m_srb.get());
        
        // --- 4.7.5 设置渲染通道描述符 (Render Pass Descriptor) ---
        // 渲染通道描述符定义了渲染过程的环境
        //
        // 渲染通道的职责：
        // 1. 定义附件（Attachments）：
        //    - 颜色缓冲区（Color Buffer）
        //    - 深度缓冲区（Depth Buffer）
        //    - 模板缓冲区（Stencil Buffer）
        //
        // 2. 定义加载/存储操作（Load/Store Operations）：
        //    - Load: 帧开始时如何初始化缓冲区
        //      - Clear: 清空为指定颜色
        //      - Load: 保留上一帧内容
        //      - DontCare: 不关心（性能最优）
        //
        //    - Store: 帧结束时如何处理缓冲区
        //      - Store: 保存回内存
        //      - Discard: 丢弃（不保存）
        //
        // 3. 定义采样数量（Samples）：
        //    - 1: 无抗锯齿
        //    - 4: 4x MSAA（多采样抗锯齿）
        //
        // 为什么使用交换链的 RenderPassDescriptor？
        // - 交换链管理屏幕显示，提供当前帧的渲染目标
        // - 自动处理缓冲区交换和显示
        // - 这是渲染到屏幕的标准方式
        m_pipeline->setRenderPassDescriptor(swapChain->currentFrameRenderTarget()->renderPassDescriptor());
        
        // --- 4.7.6 创建管线对象 ---
        // 最后一步：创建并编译管线
        //
        // create() 做了什么？
        // 1. 验证所有配置的合法性
        //    - 着色器与输入布局兼容
        //    - 资源绑定正确
        //    - 混合模式有效
        //
        // 2. 在 GPU 驱动中编译优化
        //    - 编译着色器为机器码
        //    - 优化管线配置
        //    - 分配内部资源
        //
        // 3. 创建底层 API 对象
        //    - OpenGL: glProgram + glPipeline
        //    - Vulkan: VkPipeline
        //    - D3D12: ID3D12PipelineState
        //
        // 注意：
        // - create() 是耗时操作，不应每帧调用
        // - 这就是为什么使用延迟初始化（只在第一帧创建）
        // - 后续帧复用已创建的管线对象
        m_pipeline->create();
    }

    const quint32 requiredVertexBytes = qMax<quint32>(quint32(sizeof(PlotVertex)), quint32(m_vertices.size() * sizeof(PlotVertex)));
    if (requiredVertexBytes > m_vertexBufferCapacity) {
        m_vertexBufferCapacity = requiredVertexBytes;
        m_vertexBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, m_vertexBufferCapacity));
        m_vertexBuffer->create();
    }

    if (!m_vertices.isEmpty()) {
        resourceUpdates->updateDynamicBuffer(m_vertexBuffer.get(),
                                             0,
                                             m_vertices.size() * sizeof(PlotVertex),
                                             m_vertices.constData());
    }

    resourceUpdates->updateDynamicBuffer(m_uniformBuffer.get(), 0, sizeof(PlotUniforms), &m_uniforms);

    // ===== 步骤 6：应用资源更新 =====
    // 获取当前帧的命令缓冲区
    // 命令缓冲区用于录制绘制命令
    swapChain->currentFrameCommandBuffer()->resourceUpdate(resourceUpdates);
}
//! [frame-start]

//! [frame-render]
// 主渲染通道记录开始：执行绘制操作
void PlotRenderer::mainPassRecordingStart()
{
    QRhi *rhi = m_window->rhi();
    QRhiSwapChain *swapChain = m_window->swapChain();
    if (!rhi || !swapChain || !m_pipeline || m_vertices.isEmpty())
        return;

    // 设置视口
    const QSize outputPixelSize = swapChain->currentFrameRenderTarget()->pixelSize();
    QRhiCommandBuffer *cb = m_window->swapChain()->currentFrameCommandBuffer();
    cb->setViewport({ 0.0f, 0.0f, float(outputPixelSize.width()), float(outputPixelSize.height()) });
    
    // 设置图形管线和资源
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setShaderResources();
    
    // 设置顶点输入并绘制
    const QRhiCommandBuffer::VertexInput vbufBinding(m_vertexBuffer.get(), 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(m_vertices.size());
}
//! [frame-render]

// 设置曲线管理器
void RhiPlot::setCurveManager(QSharedPointer<PlotCurveManager> manager)
{
    if (m_curveManager != manager) {
        if (m_curveManager) {
            disconnect(m_curveManager.data(), &PlotCurveManager::curvesChanged,
                       this, &RhiPlot::handleCurvesChanged);
        }

        m_curveManager = manager;

        if (m_curveManager) {
            connect(m_curveManager.data(), &PlotCurveManager::curvesChanged,
                    this, &RhiPlot::handleCurvesChanged);
        }

        emit curveManagerChanged();
        emit dataChanged();
        schedulePlotUpdate();
    }
}

// 设置自动调整范围
void RhiPlot::setAutoRange(bool autoRange)
{
    if (m_autoRange != autoRange) {
        m_autoRange = autoRange;
        emit autoRangeChanged();

        if (m_autoRange) {
            updateAxisRange();
        }
        schedulePlotUpdate();
    }
}

// 设置X轴最小值
void RhiPlot::setXMin(qreal min)
{
    if (!qFuzzyCompare(m_xMin, min)) {
        m_xMin = min;
        emit xRangeChanged();
        schedulePlotUpdate();
    }
}

// 设置X轴最大值
void RhiPlot::setXMax(qreal max)
{
    if (!qFuzzyCompare(m_xMax, max)) {
        m_xMax = max;
        emit xRangeChanged();
        schedulePlotUpdate();
    }
}

// 设置Y轴最小值
void RhiPlot::setYMin(qreal min)
{
    if (!qFuzzyCompare(m_yMin, min)) {
        m_yMin = min;
        emit yRangeChanged();
        schedulePlotUpdate();
    }
}

// 设置Y轴最大值
void RhiPlot::setYMax(qreal max)
{
    if (!qFuzzyCompare(m_yMax, max)) {
        m_yMax = max;
        emit yRangeChanged();
        schedulePlotUpdate();
    }
}

// 设置背景颜色
void RhiPlot::setBackgroundColor(const QColor &color)
{
    if (m_backgroundColor != color) {
        m_backgroundColor = color;
        emit backgroundColorChanged();

        if (window()) {
            window()->setColor(color);
        }
        schedulePlotUpdate();
    }
}

// 设置网格线可见性
void RhiPlot::setGridVisible(bool visible)
{
    if (m_gridVisible != visible) {
        m_gridVisible = visible;
        emit gridVisibleChanged();
        schedulePlotUpdate();
    }
}

// 设置网格线颜色
void RhiPlot::setGridColor(const QColor &color)
{
    if (m_gridColor != color) {
        m_gridColor = color;
        emit gridColorChanged();
        schedulePlotUpdate();
    }
}

// 设置坐标轴可见性
void RhiPlot::setAxisVisible(bool visible)
{
    if (m_axisVisible != visible) {
        m_axisVisible = visible;
        emit axisVisibleChanged();
        schedulePlotUpdate();
    }
}

// 设置坐标轴颜色
void RhiPlot::setAxisColor(const QColor &color)
{
    if (m_axisColor != color) {
        m_axisColor = color;
        emit axisColorChanged();
        schedulePlotUpdate();
    }
}

void RhiPlot::setPlotLeftMargin(qreal margin)
{
    margin = qMax<qreal>(0.0, margin);
    if (!qFuzzyCompare(m_plotLeftMargin, margin)) {
        m_plotLeftMargin = margin;
        emit plotMarginsChanged();
        schedulePlotUpdate();
    }
}

void RhiPlot::setPlotRightMargin(qreal margin)
{
    margin = qMax<qreal>(0.0, margin);
    if (!qFuzzyCompare(m_plotRightMargin, margin)) {
        m_plotRightMargin = margin;
        emit plotMarginsChanged();
        schedulePlotUpdate();
    }
}

void RhiPlot::setPlotTopMargin(qreal margin)
{
    margin = qMax<qreal>(0.0, margin);
    if (!qFuzzyCompare(m_plotTopMargin, margin)) {
        m_plotTopMargin = margin;
        emit plotMarginsChanged();
        schedulePlotUpdate();
    }
}

void RhiPlot::setPlotBottomMargin(qreal margin)
{
    margin = qMax<qreal>(0.0, margin);
    if (!qFuzzyCompare(m_plotBottomMargin, margin)) {
        m_plotBottomMargin = margin;
        emit plotMarginsChanged();
        schedulePlotUpdate();
    }
}

void RhiPlot::setXAxisTitle(const QString &title)
{
    if (m_xAxisTitle != title) {
        m_xAxisTitle = title;
        emit axisTitlesChanged();
        if (m_axisOverlay)
            m_axisOverlay->update();
        schedulePlotUpdate();
    }
}

void RhiPlot::setYAxisTitle(const QString &title)
{
    if (m_yAxisTitle != title) {
        m_yAxisTitle = title;
        emit axisTitlesChanged();
        if (m_axisOverlay)
            m_axisOverlay->update();
        schedulePlotUpdate();
    }
}

void RhiPlot::setAxisTextColor(const QColor &color)
{
    if (m_axisTextColor != color) {
        m_axisTextColor = color;
        emit axisTextColorChanged();
        if (m_axisOverlay)
            m_axisOverlay->update();
        schedulePlotUpdate();
    }
}

void RhiPlot::setZoomEnabled(bool enabled)
{
    if (m_zoomEnabled != enabled) {
        m_zoomEnabled = enabled;
        emit zoomEnabledChanged();
    }
}

void RhiPlot::setFollowLatestX(bool enabled)
{
    if (m_followLatestX != enabled) {
        m_followLatestX = enabled;
        emit followLatestXChanged();
        if (m_axisOverlay)
            m_axisOverlay->update();
        schedulePlotUpdate();
    }
}

void RhiPlot::setRefreshRate(qreal fps)
{
    if (!qIsFinite(fps))
        fps = 30.0;
    fps = qMax<qreal>(0.0, fps);

    if (qFuzzyCompare(m_refreshRate, fps))
        return;

    m_refreshRate = fps;
    if (m_refreshRate > 0.0) {
        m_refreshTimer.setInterval(qMax(1, qRound(1000.0 / m_refreshRate)));
    } else {
        m_refreshTimer.stop();
        flushPendingPlotUpdate();
    }
    emit refreshRateChanged();
}

void RhiPlot::setXAxisLabelMode(XAxisLabelMode mode)
{
    if (m_xAxisLabelMode != mode) {
        m_xAxisLabelMode = mode;
        emit xAxisLabelModeChanged();
        if (m_axisOverlay)
            m_axisOverlay->update();
        schedulePlotUpdate();
    }
}

void RhiPlot::setHoverPopupEnabled(bool enabled)
{
    if (m_hoverPopupEnabled != enabled) {
        m_hoverPopupEnabled = enabled;
        emit hoverPopupEnabledChanged();
        if (m_axisOverlay)
            m_axisOverlay->update();
        schedulePlotUpdate();
    }
}

// 处理曲线数据变化
void RhiPlot::handleCurvesChanged()
{
    if (m_autoRange) {
        updateAxisRange();
    }

    emit dataChanged();
    schedulePlotUpdate();
}

// 更新坐标轴范围
void RhiPlot::updateAxisRange()
{
    if (!m_curveManager || !m_autoRange) {
        return;
    }

    qreal minX, maxX, minY, maxY;
    if (m_curveManager->getDataRange(minX, maxX, minY, maxY)) {
        // 添加一些边距
        qreal xRange = maxX - minX;
        qreal yRange = maxY - minY;

        if (xRange < 1e-10) {
            minX -= 0.5;
            maxX += 0.5;
        } else {
            minX -= xRange * 0.05;
            maxX += xRange * 0.05;
        }

        if (yRange < 1e-10) {
            minY -= 0.5;
            maxY += 0.5;
        } else {
            minY -= yRange * 0.05;
            maxY += yRange * 0.05;
        }

        bool changed = false;
        if (!qFuzzyCompare(m_xMin, minX)) {
            m_xMin = minX;
            changed = true;
        }
        if (!qFuzzyCompare(m_xMax, maxX)) {
            m_xMax = maxX;
            changed = true;
        }
        if (!qFuzzyCompare(m_yMin, minY)) {
            m_yMin = minY;
            changed = true;
        }
        if (!qFuzzyCompare(m_yMax, maxY)) {
            m_yMax = maxY;
            changed = true;
        }

        if (changed) {
            emit xRangeChanged();
            emit yRangeChanged();
        }
    }
}

// ==================== QML 可调用接口实现 ====================

// 添加曲线
QSharedPointer<PlotCurveData> RhiPlot::addCurve(const QString &name)
{
    if (!m_curveManager) {
        return nullptr;
    }

    auto curve = m_curveManager->addCurve(name);
    return curve;
}

// 移除曲线（按索引）
void RhiPlot::removeCurve(int index)
{
    if (m_curveManager) {
        m_curveManager->removeCurve(index);
    }
}

// 移除曲线（按名称）
void RhiPlot::removeCurve(const QString &name)
{
    if (m_curveManager) {
        m_curveManager->removeCurve(name);
    }
}

// 清空所有曲线
void RhiPlot::clearCurves()
{
    if (m_curveManager) {
        m_curveManager->clear();
    }
}

// 获取曲线数量
int RhiPlot::curveCount() const
{
    return m_curveManager ? m_curveManager->curveCount() : 0;
}

// 获取曲线（按索引）
QSharedPointer<PlotCurveData> RhiPlot::getCurve(int index) const
{
    return m_curveManager ? m_curveManager->curve(index) : nullptr;
}

// 获取曲线（按名称）
QSharedPointer<PlotCurveData> RhiPlot::getCurve(const QString &name) const
{
    return m_curveManager ? m_curveManager->curve(name) : nullptr;
}

// 添加数据点（按索引）
void RhiPlot::addDataPoint(int curveIndex, qreal x, qreal y)
{
    auto curve = getCurve(curveIndex);
    if (curve) {
        curve->addPoint(x, y);
    }
}

// 添加数据点（按名称）
void RhiPlot::addDataPoint(const QString &curveName, qreal x, qreal y)
{
    auto curve = getCurve(curveName);
    if (curve) {
        curve->addPoint(x, y);
    }
}

// 设置数据（按索引）
void RhiPlot::setData(int curveIndex, const QVector<qreal> &xData, const QVector<qreal> &yData)
{
    auto curve = getCurve(curveIndex);
    if (curve) {
        curve->setData(xData, yData);
    }
}

// 设置数据（按名称）
void RhiPlot::setData(const QString &curveName, const QVector<qreal> &xData, const QVector<qreal> &yData)
{
    auto curve = getCurve(curveName);
    if (curve) {
        curve->setData(xData, yData);
    }
}

void RhiPlot::clearHistoryData()
{
    if (!m_curveManager)
        return;

    for (const auto &curve : m_curveManager->curves()) {
        if (curve)
            curve->clear();
    }
    clearHoverInfo();
    emit dataChanged();
    schedulePlotUpdate();
}

bool RhiPlot::exportData(const QString &directoryPath, const QString &fileName) const
{
    if (!m_curveManager)
        return false;

    const QString exportDirectory = resolvedExportDirectory(directoryPath);
    QDir dir(exportDirectory);
    if (!dir.exists() && !dir.mkpath(".")) {
        qWarning() << "RhiPlot export failed: cannot create directory" << exportDirectory;
        return false;
    }

    QString safeFileName = QFileInfo(fileName.trimmed()).fileName();
    if (safeFileName.isEmpty()) {
        safeFileName = QStringLiteral("rhiplot_export_%1.csv")
                           .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    } else if (QFileInfo(safeFileName).suffix().isEmpty()) {
        safeFileName += QStringLiteral(".csv");
    }

    QFile file(dir.filePath(safeFileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "RhiPlot export failed: cannot open file" << file.fileName() << file.errorString();
        return false;
    }

    QTextStream out(&file);
    const QVector<QSharedPointer<PlotCurveData>> curves = m_curveManager->curves();
    QVector<qreal> xValues;
    for (const auto &curve : curves) {
        if (!curve)
            continue;
        const QVector<QPointF> &points = curve->dataRef();
        xValues.reserve(xValues.size() + points.size());
        for (const QPointF &point : points)
            xValues.append(point.x());
    }

    std::sort(xValues.begin(), xValues.end());
    auto uniqueEnd = std::unique(xValues.begin(), xValues.end(), [](qreal left, qreal right) {
        return qAbs(left - right) <= 1e-9;
    });
    xValues.erase(uniqueEnd, xValues.end());

    out << csvEscaped(m_xAxisTitle.isEmpty() ? QStringLiteral("x") : m_xAxisTitle);
    for (qsizetype curveIndex = 0; curveIndex < curves.size(); ++curveIndex) {
        const auto &curve = curves.at(curveIndex);
        QString curveName = curve ? curve->name() : QString();
        if (curveName.isEmpty())
            curveName = QStringLiteral("Curve %1").arg(curveIndex + 1);
        out << ',' << csvEscaped(curveName);
    }
    out << '\n';

    QVector<qsizetype> pointIndexes(curves.size(), 0);
    for (qreal xValue : xValues) {
        out << QString::number(xValue, 'f', 6);
        for (qsizetype curveIndex = 0; curveIndex < curves.size(); ++curveIndex) {
            out << ',';
            const auto &curve = curves.at(curveIndex);
            if (!curve)
                continue;

            const QVector<QPointF> &points = curve->dataRef();
            qsizetype &pointIndex = pointIndexes[curveIndex];
            while (pointIndex < points.size() && points.at(pointIndex).x() < xValue - 1e-9)
                ++pointIndex;
            if (pointIndex < points.size() && qAbs(points.at(pointIndex).x() - xValue) <= 1e-9)
                out << QString::number(points.at(pointIndex).y(), 'g', 15);
        }
        out << '\n';
    }

    return out.status() == QTextStream::Ok;
}

bool RhiPlot::exportDataFile(const QUrl &fileUrl) const
{
    QString filePath = fileUrl.isLocalFile()
                           ? fileUrl.toLocalFile()
                           : fileUrl.toString(QUrl::PreferLocalFile);
    filePath = filePath.trimmed();
    if (filePath.isEmpty()) {
        qWarning() << "RhiPlot export failed: empty file path";
        return false;
    }

    const QFileInfo fileInfo(filePath);
    return exportData(fileInfo.absolutePath(), fileInfo.fileName());
}

// 设置曲线颜色（按索引）
void RhiPlot::setCurveColor(int curveIndex, const QColor &color)
{
    auto curve = getCurve(curveIndex);
    if (curve && curve->appearance()) {
        curve->appearance()->setLineColor(color);
        curve->appearance()->setScatterColor(color);
    }
}

// 设置曲线颜色（按名称）
void RhiPlot::setCurveColor(const QString &curveName, const QColor &color)
{
    auto curve = getCurve(curveName);
    if (curve && curve->appearance()) {
        curve->appearance()->setLineColor(color);
        curve->appearance()->setScatterColor(color);
    }
}

// 设置曲线宽度（按索引）
void RhiPlot::setCurveWidth(int curveIndex, qreal width)
{
    auto curve = getCurve(curveIndex);
    if (curve && curve->appearance()) {
        curve->appearance()->setLineWidth(width);
    }
}

// 设置曲线宽度（按名称）
void RhiPlot::setCurveWidth(const QString &curveName, qreal width)
{
    auto curve = getCurve(curveName);
    if (curve && curve->appearance()) {
        curve->appearance()->setLineWidth(width);
    }
}

// 设置曲线样式（按索引）
void RhiPlot::setCurveStyle(int curveIndex, LineStyle style)
{
    auto curve = getCurve(curveIndex);
    if (curve && curve->appearance()) {
        curve->appearance()->setLineStyle(style);
    }
}

// 设置曲线样式（按名称）
void RhiPlot::setCurveStyle(const QString &curveName, LineStyle style)
{
    auto curve = getCurve(curveName);
    if (curve && curve->appearance()) {
        curve->appearance()->setLineStyle(style);
    }
}

void RhiPlot::setCurveLineStyle(int curveIndex, CurveLineStyle style)
{
    setCurveStyle(curveIndex, static_cast<LineStyle>(style));
}

void RhiPlot::setCurveLineStyle(const QString &curveName, CurveLineStyle style)
{
    setCurveStyle(curveName, static_cast<LineStyle>(style));
}

// 设置曲线可见性（按索引）
void RhiPlot::setCurveVisible(int curveIndex, bool visible)
{
    auto curve = getCurve(curveIndex);
    if (curve && curve->appearance()) {
        curve->appearance()->setVisible(visible);
    }
}

// 设置曲线可见性（按名称）
void RhiPlot::setCurveVisible(const QString &curveName, bool visible)
{
    auto curve = getCurve(curveName);
    if (curve && curve->appearance()) {
        curve->appearance()->setVisible(visible);
    }
}

// 自动调整坐标轴范围
void RhiPlot::autoAdjustRange()
{
    updateAxisRange();
}

// 更新绘图
void RhiPlot::updatePlot()
{
    if (window()) {
        window()->update();
    }
}


#include "rhiplot.moc"
