#ifndef PLOTDATA_H
#define PLOTDATA_H

#include <QObject>
#include <QVector>
#include <QPointF>
#include <QColor>
#include <QPen>
#include <QBrush>
#include <QSharedPointer>

// 线型枚举
enum class LineStyle {
    SolidLine = 0,      // 实线
    DashLine,           // 虚线
    DotLine,            // 点线
    DashDotLine,        // 点划线
    DashDotDotLine,     // 双点划线
    NoLine              // 无线
};

// 点样式枚举
enum class ScatterStyle {
    None = 0,           // 无点
    Dot,                // 点
    Cross,              // 十字
    Plus,               // 加号
    Circle,             // 圆圈
    Disc,               // 实心圆
    Square,             // 正方形
    Diamond,            // 菱形
    Star,               // 星形
    Triangle,           // 三角形
    TriangleInverted,   // 倒三角形
    CrossSquare,        // 十字方形
    PlusSquare,         // 加号方形
    CrossCircle,        // 十字圆圈
    PlusCircle,         // 加号圆圈
    Peace               // 和平符号
};

// 曲线外观配置类
class PlotCurveAppearance : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY lineColorChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(LineStyle lineStyle READ lineStyle WRITE setLineStyle NOTIFY lineStyleChanged)
    Q_PROPERTY(ScatterStyle scatterStyle READ scatterStyle WRITE setScatterStyle NOTIFY scatterStyleChanged)
    Q_PROPERTY(QColor scatterColor READ scatterColor WRITE setScatterColor NOTIFY scatterColorChanged)
    Q_PROPERTY(qreal scatterSize READ scatterSize WRITE setScatterSize NOTIFY scatterSizeChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

public:
    explicit PlotCurveAppearance(QObject *parent = nullptr);
    
    // 获取和设置线颜色
    QColor lineColor() const { return m_lineColor; }
    void setLineColor(const QColor &color);
    
    // 获取和设置线宽
    qreal lineWidth() const { return m_lineWidth; }
    void setLineWidth(qreal width);
    
    // 获取和设置线型
    LineStyle lineStyle() const { return m_lineStyle; }
    void setLineStyle(LineStyle style);
    
    // 获取和设置点样式
    ScatterStyle scatterStyle() const { return m_scatterStyle; }
    void setScatterStyle(ScatterStyle style);
    
    // 获取和设置点颜色
    QColor scatterColor() const { return m_scatterColor; }
    void setScatterColor(const QColor &color);
    
    // 获取和设置点大小
    qreal scatterSize() const { return m_scatterSize; }
    void setScatterSize(qreal size);
    
    // 获取和设置可见性
    bool visible() const { return m_visible; }
    void setVisible(bool visible);
    
    // 获取和设置透明度
    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);
    
    // 转换为 QPen
    QPen toPen() const;
    
    // 转换为 QBrush（用于填充点）
    QBrush toBrush() const;

signals:
    void lineColorChanged();
    void lineWidthChanged();
    void lineStyleChanged();
    void scatterStyleChanged();
    void scatterColorChanged();
    void scatterSizeChanged();
    void visibleChanged();
    void opacityChanged();
    void appearanceChanged(); // 任何外观变化时发出

private:
    QColor m_lineColor = Qt::blue;
    qreal m_lineWidth = 1.0;
    LineStyle m_lineStyle = LineStyle::SolidLine;
    ScatterStyle m_scatterStyle = ScatterStyle::None;
    QColor m_scatterColor = Qt::blue;
    qreal m_scatterSize = 6.0;
    bool m_visible = true;
    qreal m_opacity = 1.0;
};

// 单条曲线数据类
class PlotCurveData : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QSharedPointer<PlotCurveAppearance> appearance READ appearance WRITE setAppearance NOTIFY appearanceChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY dataChanged)

public:
    explicit PlotCurveData(const QString &name = QString(), QObject *parent = nullptr);
    
    // 获取和设置曲线名称
    QString name() const { return m_name; }
    void setName(const QString &name);
    
    // 获取和设置外观配置
    QSharedPointer<PlotCurveAppearance> appearance() const { return m_appearance; }
    void setAppearance(QSharedPointer<PlotCurveAppearance> appearance);
    
    // 数据点操作
    int pointCount() const { return m_data.size(); }
    bool isEmpty() const { return m_data.isEmpty(); }
    
    // 添加数据点
    void addPoint(qreal x, qreal y);
    void addPoint(const QPointF &point);
    void addPoints(const QVector<QPointF> &points);
    
    // 设置数据点（替换现有数据）
    void setData(const QVector<QPointF> &data);
    void setData(const QVector<qreal> &xData, const QVector<qreal> &yData);
    
    // 获取数据点
    QVector<QPointF> data() const { return m_data; }
    const QVector<QPointF> &dataRef() const { return m_data; }
    QPointF pointAt(int index) const;
    int lowerBoundX(qreal x) const;
    int upperBoundX(qreal x) const;
    
    // 获取数据范围
    qreal minX() const;
    qreal maxX() const;
    qreal minY() const;
    qreal maxY() const;

    struct DataRange
    {
        qreal minX = 0;
        qreal maxX = 0;
        qreal minY = 0;
        qreal maxY = 0;
        bool valid = false;
    };

    DataRange rangeByX(qreal minX, qreal maxX) const;
    DataRange rangeByY(qreal minY, qreal maxY) const;

    // 获取X范围内的数据点 // 获取Y范围内的数据点 // 获取XY矩形范围内的数据点
    QVector<QPointF> pointsByX(qreal minX, qreal maxX) const;
    QVector<QPointF> pointsByY(qreal minY, qreal maxY) const;
    QVector<QPointF> pointsInRange(qreal minX, qreal maxX, qreal minY, qreal maxY) const;
    
    // 清空数据
    void clear();
    
    // 移除指定索引的点
    void removePoint(int index);
    
    // 更新指定索引的点
    void updatePoint(int index, qreal x, qreal y);
    void updatePoint(int index, const QPointF &point);

signals:
    void nameChanged();
    void appearanceChanged();
    void dataChanged();
    void pointAdded(int index);
    void pointRemoved(int index);
    void pointUpdated(int index);

private:
    QString m_name;
    QVector<QPointF> m_data;
    QSharedPointer<PlotCurveAppearance> m_appearance;
    
    // 缓存的数据范围
    mutable bool m_rangeValid = false;
    mutable qreal m_minX = 0, m_maxX = 0, m_minY = 0, m_maxY = 0;
    
    // 更新数据范围缓存
    void updateRange() const;
};

// 曲线管理器类（管理多条曲线）
class PlotCurveManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int curveCount READ curveCount NOTIFY curvesChanged)

public:
    explicit PlotCurveManager(QObject *parent = nullptr);
    
    // 曲线数量
    int curveCount() const { return m_curves.size(); }
    
    // 添加曲线
    QSharedPointer<PlotCurveData> addCurve(const QString &name = QString());
    void addCurve(QSharedPointer<PlotCurveData> curve);
    
    // 获取曲线
    QSharedPointer<PlotCurveData> curve(int index) const;
    QSharedPointer<PlotCurveData> curve(const QString &name) const;
    
    // 移除曲线
    void removeCurve(int index);
    void removeCurve(const QString &name);
    void removeCurve(QSharedPointer<PlotCurveData> curve);
    
    // 清空所有曲线
    void clear();
    
    // 获取所有曲线
    QVector<QSharedPointer<PlotCurveData>> curves() const { return m_curves; }
    
    // 查找曲线索引
    int indexOf(QSharedPointer<PlotCurveData> curve) const;
    int indexOf(const QString &name) const;
    
    // 获取所有曲线的数据范围
    bool getDataRange(qreal &minX, qreal &maxX, qreal &minY, qreal &maxY) const;

signals:
    void curvesChanged();
    void curveAdded(int index);
    void curveRemoved(int index);

private:
    QVector<QSharedPointer<PlotCurveData>> m_curves;
};

#endif // PLOTDATA_H
