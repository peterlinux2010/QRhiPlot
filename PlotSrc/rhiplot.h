#ifndef RHIPLOT_H
#define RHIPLOT_H

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQml/qqmlregistration.h>
#include <QVariantList>
#include <QUrl>
#include <QTimer>

#include "plotdata.h"

class QWheelEvent;
class QMouseEvent;
class PlotRenderer;
class PlotAxisOverlay;

//! [0]
// RhiPlot 是一个基于 Qt RHI 的绘图组件，继承自 QQuickItem
// 该类提供了一个可在 QML 中使用的绘图组件，通过 PlotRenderer 进行实际的渲染操作
class RhiPlot : public QQuickItem
{
    Q_OBJECT
    // 曲线管理器属性
    Q_PROPERTY(QSharedPointer<PlotCurveManager> curveManager READ curveManager WRITE setCurveManager NOTIFY curveManagerChanged)
    // 自动调整坐标轴范围
    Q_PROPERTY(bool autoRange READ autoRange WRITE setAutoRange NOTIFY autoRangeChanged)
    // X轴范围
    Q_PROPERTY(qreal xMin READ xMin WRITE setXMin NOTIFY xRangeChanged)
    Q_PROPERTY(qreal xMax READ xMax WRITE setXMax NOTIFY xRangeChanged)
    // Y轴范围
    Q_PROPERTY(qreal yMin READ yMin WRITE setYMin NOTIFY yRangeChanged)
    Q_PROPERTY(qreal yMax READ yMax WRITE setYMax NOTIFY yRangeChanged)
    // 背景颜色
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    // 网格线可见性
    Q_PROPERTY(bool gridVisible READ gridVisible WRITE setGridVisible NOTIFY gridVisibleChanged)
    // 网格线颜色
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)
    // 坐标轴可见性
    Q_PROPERTY(bool axisVisible READ axisVisible WRITE setAxisVisible NOTIFY axisVisibleChanged)
    // 坐标轴颜色
    Q_PROPERTY(QColor axisColor READ axisColor WRITE setAxisColor NOTIFY axisColorChanged)
    // 绘图区内边距，为坐标轴、刻度和注释留出空间
    Q_PROPERTY(qreal plotLeftMargin READ plotLeftMargin WRITE setPlotLeftMargin NOTIFY plotMarginsChanged)
    Q_PROPERTY(qreal plotRightMargin READ plotRightMargin WRITE setPlotRightMargin NOTIFY plotMarginsChanged)
    Q_PROPERTY(qreal plotTopMargin READ plotTopMargin WRITE setPlotTopMargin NOTIFY plotMarginsChanged)
    Q_PROPERTY(qreal plotBottomMargin READ plotBottomMargin WRITE setPlotBottomMargin NOTIFY plotMarginsChanged)
    Q_PROPERTY(QString xAxisTitle READ xAxisTitle WRITE setXAxisTitle NOTIFY axisTitlesChanged)
    Q_PROPERTY(QString yAxisTitle READ yAxisTitle WRITE setYAxisTitle NOTIFY axisTitlesChanged)
    Q_PROPERTY(QColor axisTextColor READ axisTextColor WRITE setAxisTextColor NOTIFY axisTextColorChanged)
    Q_PROPERTY(bool zoomEnabled READ zoomEnabled WRITE setZoomEnabled NOTIFY zoomEnabledChanged)
    Q_PROPERTY(bool followLatestX READ followLatestX WRITE setFollowLatestX NOTIFY followLatestXChanged)
    Q_PROPERTY(qreal refreshRate READ refreshRate WRITE setRefreshRate NOTIFY refreshRateChanged)
    Q_PROPERTY(XAxisLabelMode xAxisLabelMode READ xAxisLabelMode WRITE setXAxisLabelMode NOTIFY xAxisLabelModeChanged)
    Q_PROPERTY(bool hoverInfoValid READ hoverInfoValid NOTIFY hoverInfoChanged)
    Q_PROPERTY(qreal hoverX READ hoverX NOTIFY hoverInfoChanged)
    Q_PROPERTY(QPointF hoverPosition READ hoverPosition NOTIFY hoverInfoChanged)
    Q_PROPERTY(QVariantList hoverValues READ hoverValues NOTIFY hoverInfoChanged)
    Q_PROPERTY(bool hoverPopupEnabled READ hoverPopupEnabled WRITE setHoverPopupEnabled NOTIFY hoverPopupEnabledChanged)
    // 每条曲线保留的最大点数；<=0 表示不限制。设置后立即应用到所有曲线。
    Q_PROPERTY(int maxPoints READ maxPoints WRITE setMaxPoints NOTIFY maxPointsChanged)

    QML_ELEMENT

public:
    enum CurveLineStyle {
        SolidLine = static_cast<int>(LineStyle::SolidLine),
        DashLine = static_cast<int>(LineStyle::DashLine),
        DotLine = static_cast<int>(LineStyle::DotLine),
        DashDotLine = static_cast<int>(LineStyle::DashDotLine),
        DashDotDotLine = static_cast<int>(LineStyle::DashDotDotLine),
        NoLine = static_cast<int>(LineStyle::NoLine)
    };
    Q_ENUM(CurveLineStyle)

    enum XAxisLabelMode {
        NumericXAxis = 0,
        TimeXAxis
    };
    Q_ENUM(XAxisLabelMode)

    // 构造函数：初始化 RhiPlot 实例
    RhiPlot();

    // 曲线管理器
    QSharedPointer<PlotCurveManager> curveManager() const { return m_curveManager; }
    void setCurveManager(QSharedPointer<PlotCurveManager> manager);

    // 自动调整范围
    bool autoRange() const { return m_autoRange; }
    void setAutoRange(bool autoRange);

    // X轴范围
    qreal xMin() const { return m_xMin; }
    void setXMin(qreal min);
    qreal xMax() const { return m_xMax; }
    void setXMax(qreal max);

    // Y轴范围
    qreal yMin() const { return m_yMin; }
    void setYMin(qreal min);
    qreal yMax() const { return m_yMax; }
    void setYMax(qreal max);

    // 背景颜色
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &color);

    // 网格线
    bool gridVisible() const { return m_gridVisible; }
    void setGridVisible(bool visible);
    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor &color);

    // 坐标轴
    bool axisVisible() const { return m_axisVisible; }
    void setAxisVisible(bool visible);
    QColor axisColor() const { return m_axisColor; }
    void setAxisColor(const QColor &color);

    qreal plotLeftMargin() const { return m_plotLeftMargin; }
    void setPlotLeftMargin(qreal margin);
    qreal plotRightMargin() const { return m_plotRightMargin; }
    void setPlotRightMargin(qreal margin);
    qreal plotTopMargin() const { return m_plotTopMargin; }
    void setPlotTopMargin(qreal margin);
    qreal plotBottomMargin() const { return m_plotBottomMargin; }
    void setPlotBottomMargin(qreal margin);

    QString xAxisTitle() const { return m_xAxisTitle; }
    void setXAxisTitle(const QString &title);
    QString yAxisTitle() const { return m_yAxisTitle; }
    void setYAxisTitle(const QString &title);
    QColor axisTextColor() const { return m_axisTextColor; }
    void setAxisTextColor(const QColor &color);
    bool zoomEnabled() const { return m_zoomEnabled; }
    void setZoomEnabled(bool enabled);
    bool followLatestX() const { return m_followLatestX; }
    void setFollowLatestX(bool enabled);
    qreal refreshRate() const { return m_refreshRate; }
    void setRefreshRate(qreal fps);
    XAxisLabelMode xAxisLabelMode() const { return m_xAxisLabelMode; }
    void setXAxisLabelMode(XAxisLabelMode mode);
    bool hoverInfoValid() const { return m_hoverInfoValid; }
    qreal hoverX() const { return m_hoverX; }
    QPointF hoverPosition() const { return m_hoverPosition; }
    QVariantList hoverValues() const { return m_hoverValues; }
    bool hoverPopupEnabled() const { return m_hoverPopupEnabled; }
    void setHoverPopupEnabled(bool enabled);

    int maxPoints() const { return m_maxPoints; }
    void setMaxPoints(int value);

    // QML 可调用接口
    Q_INVOKABLE QSharedPointer<PlotCurveData> addCurve(const QString &name = QString());
    Q_INVOKABLE void removeCurve(int index);
    Q_INVOKABLE void removeCurve(const QString &name);
    Q_INVOKABLE void clearCurves();
    Q_INVOKABLE int curveCount() const;
    Q_INVOKABLE QSharedPointer<PlotCurveData> getCurve(int index) const;
    Q_INVOKABLE QSharedPointer<PlotCurveData> getCurve(const QString &name) const;

    // 数据操作接口
    Q_INVOKABLE void addDataPoint(int curveIndex, qreal x, qreal y);
    Q_INVOKABLE void addDataPoint(const QString &curveName, qreal x, qreal y);
    Q_INVOKABLE void setData(int curveIndex, const QVector<qreal> &xData, const QVector<qreal> &yData);
    Q_INVOKABLE void setData(const QString &curveName, const QVector<qreal> &xData, const QVector<qreal> &yData);
    Q_INVOKABLE void clearHistoryData();
    Q_INVOKABLE bool exportData(const QString &directoryPath, const QString &fileName) const;
    Q_INVOKABLE bool exportDataFile(const QUrl &fileUrl) const;

    // 外观设置接口
    Q_INVOKABLE void setCurveColor(int curveIndex, const QColor &color);
    Q_INVOKABLE void setCurveColor(const QString &curveName, const QColor &color);
    Q_INVOKABLE void setCurveWidth(int curveIndex, qreal width);
    Q_INVOKABLE void setCurveWidth(const QString &curveName, qreal width);
    Q_INVOKABLE void setCurveStyle(int curveIndex, LineStyle style);
    Q_INVOKABLE void setCurveStyle(const QString &curveName, LineStyle style);
    Q_INVOKABLE void setCurveLineStyle(int curveIndex, CurveLineStyle style);
    Q_INVOKABLE void setCurveLineStyle(const QString &curveName, CurveLineStyle style);
    Q_INVOKABLE void setCurveVisible(int curveIndex, bool visible);
    Q_INVOKABLE void setCurveVisible(const QString &curveName, bool visible);

    // 曲线 Y 轴显示倍数（1.0 为原始比例，只影响绘制，不修改原始数据）
    Q_INVOKABLE void setCurveYScale(int curveIndex, qreal scale);
    Q_INVOKABLE void setCurveYScale(const QString &curveName, qreal scale);
    Q_INVOKABLE qreal curveYScale(int curveIndex) const;
    Q_INVOKABLE qreal curveYScale(const QString &curveName) const;

    // 自动调整坐标轴范围
    Q_INVOKABLE void autoAdjustRange();

    // 更新绘图
    Q_INVOKABLE void updatePlot();

signals:
    // 曲线管理器变化
    void curveManagerChanged();
    // 自动范围变化
    void autoRangeChanged();
    // 坐标轴范围变化
    void xRangeChanged();
    void yRangeChanged();
    // 背景颜色变化
    void backgroundColorChanged();
    // 网格线变化
    void gridVisibleChanged();
    void gridColorChanged();
    // 坐标轴变化
    void axisVisibleChanged();
    void axisColorChanged();
    void plotMarginsChanged();
    void axisTitlesChanged();
    void axisTextColorChanged();
    void zoomEnabledChanged();
    void followLatestXChanged();
    void refreshRateChanged();
    void xAxisLabelModeChanged();
    void hoverInfoChanged();
    void hoverPopupEnabledChanged();
    void maxPointsChanged();
    // 数据变化（用于触发重绘）
    void dataChanged();

public slots:
    // 同步操作：同步渲染器状态，确保绘图内容与当前状态一致
    void sync();
    
    // 清理操作：清理渲染资源，防止内存泄漏
    void cleanup();

private slots:
    // 处理窗口变化事件
    void handleWindowChanged(QQuickWindow *win);

    // 处理曲线数据变化
    void handleCurvesChanged();

private:
    // 释放资源：重写 QQuickItem 的方法
    void releaseResources() override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;

    // 更新坐标轴范围（如果启用自动调整）
    void updateAxisRange();
    void updateHoverInfo(const QPointF &position);
    void clearHoverInfo();
    qreal xValueAtPosition(qreal positionX) const;
    void schedulePlotUpdate();
    void flushPendingPlotUpdate();

    // 曲线管理器
    QSharedPointer<PlotCurveManager> m_curveManager;

    // 坐标轴范围
    bool m_autoRange = true;
    qreal m_xMin = -1.0;
    qreal m_xMax = 1.0;
    qreal m_yMin = -1.0;
    qreal m_yMax = 1.0;

    // 外观设置
    QColor m_backgroundColor = Qt::black;
    bool m_gridVisible = true;
    QColor m_gridColor = QColor(100, 100, 100, 100);
    bool m_axisVisible = true;
    QColor m_axisColor = Qt::white;
    qreal m_plotLeftMargin = 80.0;
    // 右侧不绘制任何内容（无右轴、无标签），只留少量内边距
    qreal m_plotRightMargin = 12.0;
    qreal m_plotTopMargin = 16.0;
    qreal m_plotBottomMargin = 52.0;
    QString m_xAxisTitle;
    QString m_yAxisTitle;
    QColor m_axisTextColor = Qt::white;
    bool m_zoomEnabled = true;
    bool m_followLatestX = true;
    qreal m_refreshRate = 30.0;
    bool m_plotUpdatePending = false;
    QTimer m_refreshTimer;
    XAxisLabelMode m_xAxisLabelMode = NumericXAxis;
    bool m_dragging = false;
    bool m_dragX = false;
    bool m_dragY = false;
    QPointF m_lastMousePos;
    bool m_hoverInfoValid = false;
    bool m_hoverPopupEnabled = true;
    // 每条曲线保留的最大点数，<=0 表示不限制
    int m_maxPoints = 1000000;
    qreal m_hoverX = 0.0;
    QPointF m_hoverPosition;
    QVariantList m_hoverValues;
    
    // 渲染器指针：指向实际执行渲染操作的 PlotRenderer 实例
    PlotRenderer *m_renderer = nullptr;
    PlotAxisOverlay *m_axisOverlay = nullptr;
};
//! [0]

#endif
