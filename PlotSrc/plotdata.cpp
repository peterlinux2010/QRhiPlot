#include "plotdata.h"
#include <QDebug>
#include <QtGlobal>
#include <algorithm>

// ==================== PlotCurveAppearance 实现 ====================

PlotCurveAppearance::PlotCurveAppearance(QObject *parent)
    : QObject(parent)
{
}

void PlotCurveAppearance::setLineColor(const QColor &color)
{
    if (m_lineColor != color) {
        m_lineColor = color;
        emit lineColorChanged();
        emit appearanceChanged();
    }
}

void PlotCurveAppearance::setLineWidth(qreal width)
{
    if (!qFuzzyCompare(m_lineWidth, width) && width >= 0) {
        m_lineWidth = width;
        emit lineWidthChanged();
        emit appearanceChanged();
    }
}

void PlotCurveAppearance::setLineStyle(LineStyle style)
{
    if (m_lineStyle != style) {
        m_lineStyle = style;
        emit lineStyleChanged();
        emit appearanceChanged();
    }
}

void PlotCurveAppearance::setScatterStyle(ScatterStyle style)
{
    if (m_scatterStyle != style) {
        m_scatterStyle = style;
        emit scatterStyleChanged();
        emit appearanceChanged();
    }
}

void PlotCurveAppearance::setScatterColor(const QColor &color)
{
    if (m_scatterColor != color) {
        m_scatterColor = color;
        emit scatterColorChanged();
        emit appearanceChanged();
    }
}

void PlotCurveAppearance::setScatterSize(qreal size)
{
    if (!qFuzzyCompare(m_scatterSize, size) && size >= 0) {
        m_scatterSize = size;
        emit scatterSizeChanged();
        emit appearanceChanged();
    }
}

void PlotCurveAppearance::setVisible(bool visible)
{
    if (m_visible != visible) {
        m_visible = visible;
        emit visibleChanged();
        emit appearanceChanged();
    }
}

void PlotCurveAppearance::setOpacity(qreal opacity)
{
    if (!qFuzzyCompare(m_opacity, opacity) && opacity >= 0 && opacity <= 1) {
        m_opacity = opacity;
        emit opacityChanged();
        emit appearanceChanged();
    }
}

void PlotCurveAppearance::setYScale(qreal scale)
{
    // 倍数必须为正的有限值，否则曲线会被翻转或消失
    if (!qIsFinite(scale) || scale <= 0.0)
        return;
    if (qFuzzyCompare(m_yScale, scale))
        return;

    m_yScale = scale;
    emit yScaleChanged();
    emit appearanceChanged();
}

QPen PlotCurveAppearance::toPen() const
{
    if (!m_visible || m_lineStyle == LineStyle::NoLine) {
        return QPen(Qt::NoPen);
    }
    
    QPen pen(m_lineColor);
    pen.setWidthF(m_lineWidth);
    pen.setCosmetic(true); // 使用逻辑坐标宽度，不随缩放变化
    
    // 设置线型
    switch (m_lineStyle) {
    case LineStyle::SolidLine:
        pen.setStyle(Qt::SolidLine);
        break;
    case LineStyle::DashLine:
        pen.setStyle(Qt::DashLine);
        break;
    case LineStyle::DotLine:
        pen.setStyle(Qt::DotLine);
        break;
    case LineStyle::DashDotLine:
        pen.setStyle(Qt::DashDotLine);
        break;
    case LineStyle::DashDotDotLine:
        pen.setStyle(Qt::DashDotDotLine);
        break;
    case LineStyle::NoLine:
        pen.setStyle(Qt::NoPen);
        break;
    }
    
    return pen;
}

QBrush PlotCurveAppearance::toBrush() const
{
    if (!m_visible || m_scatterStyle == ScatterStyle::None) {
        return QBrush(Qt::NoBrush);
    }
    
    QColor color = m_scatterColor;
    if (m_opacity < 1.0) {
        color.setAlphaF(color.alphaF() * m_opacity);
    }
    
    return QBrush(color);
}

// ==================== PlotCurveData 实现 ====================

PlotCurveData::PlotCurveData(const QString &name, QObject *parent)
    : QObject(parent)
    , m_name(name)
{
    m_appearance = QSharedPointer<PlotCurveAppearance>::create();
    connect(m_appearance.data(), &PlotCurveAppearance::appearanceChanged,
            this, &PlotCurveData::appearanceChanged);
}

void PlotCurveData::setName(const QString &name)
{
    if (m_name != name) {
        m_name = name;
        emit nameChanged();
    }
}

void PlotCurveData::setAppearance(QSharedPointer<PlotCurveAppearance> appearance)
{
    if (m_appearance != appearance) {
        if (m_appearance) {
            disconnect(m_appearance.data(), &PlotCurveAppearance::appearanceChanged,
                      this, &PlotCurveData::appearanceChanged);
        }
        
        m_appearance = appearance;
        
        if (m_appearance) {
            connect(m_appearance.data(), &PlotCurveAppearance::appearanceChanged,
                    this, &PlotCurveData::appearanceChanged);
        }
        
        emit appearanceChanged();
    }
}

void PlotCurveData::addPoint(qreal x, qreal y)
{
    m_data.append(QPointF(x, y));
    trimToLimit();
    m_rangeValid = false;
    emit dataChanged();
    emit pointAdded(m_data.size() - 1);
}

void PlotCurveData::addPoint(const QPointF &point)
{
    m_data.append(point);
    trimToLimit();
    m_rangeValid = false;
    emit dataChanged();
    emit pointAdded(m_data.size() - 1);
}

void PlotCurveData::addPoints(const QVector<QPointF> &points)
{
    if (points.isEmpty()) return;

    m_data.append(points);
    trimToLimit();
    m_rangeValid = false;
    emit dataChanged();
    // 批量添加时不发出单个点添加信号
}

void PlotCurveData::setMaxPoints(int value)
{
    if (value < 0) value = 0;
    if (m_maxPoints == value) return;
    m_maxPoints = value;

    // 立即按新上限裁剪已有数据，避免设置后仍占用旧内存
    if (m_maxPoints > 0 && m_data.size() > m_maxPoints) {
        m_data.remove(0, m_data.size() - m_maxPoints);
        m_rangeValid = false;
        emit dataChanged();
    }
    emit maxPointsChanged();
}

void PlotCurveData::trimToLimit()
{
    if (m_maxPoints <= 0 || m_data.size() <= m_maxPoints)
        return;

    // 不用 removeFirst() 逐点删：QVector 每次删除都要搬移后续元素，O(N)，
    // 高频采样下会明显卡顿。这里一次删掉超出量的 10%（至少 1 个），
    // 把搬移成本摊薄到每 maxPoints/10 个点才发生一次。
    const int overflow = m_data.size() - m_maxPoints;
    const int drop = qMax(overflow, qMax(1, m_maxPoints / 10));
    m_data.remove(0, qMin(drop, m_data.size()));
}

void PlotCurveData::setData(const QVector<QPointF> &data)
{
    m_data = data;
    m_rangeValid = false;
    emit dataChanged();
}

void PlotCurveData::setData(const QVector<qreal> &xData, const QVector<qreal> &yData)
{
    int count = qMin(xData.size(), yData.size());
    m_data.clear();
    m_data.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        m_data.append(QPointF(xData[i], yData[i]));
    }
    
    m_rangeValid = false;
    emit dataChanged();
}

QPointF PlotCurveData::pointAt(int index) const
{
    if (index >= 0 && index < m_data.size()) {
        return m_data[index];
    }
    return QPointF();
}

int PlotCurveData::lowerBoundX(qreal x) const
{
    int left = 0;
    int right = m_data.size();
    while (left < right) {
        const int mid = (left + right) / 2;
        if (m_data[mid].x() < x)
            left = mid + 1;
        else
            right = mid;
    }
    return left;
}

int PlotCurveData::upperBoundX(qreal x) const
{
    int left = 0;
    int right = m_data.size();
    while (left < right) {
        const int mid = (left + right) / 2;
        if (m_data[mid].x() <= x)
            left = mid + 1;
        else
            right = mid;
    }
    return left;
}

qreal PlotCurveData::minX() const
{
    if (!m_rangeValid) {
        updateRange();
    }
    return m_minX;
}

qreal PlotCurveData::maxX() const
{
    if (!m_rangeValid) {
        updateRange();
    }
    return m_maxX;
}

qreal PlotCurveData::minY() const
{
    if (!m_rangeValid) {
        updateRange();
    }
    return m_minY;
}

qreal PlotCurveData::maxY() const
{
    if (!m_rangeValid) {
        updateRange();
    }
    return m_maxY;
}

void PlotCurveData::clear()
{
    if (!m_data.isEmpty()) {
        m_data.clear();
        m_rangeValid = false;
        emit dataChanged();
    }
}

void PlotCurveData::removePoint(int index)
{
    if (index >= 0 && index < m_data.size()) {
        m_data.remove(index);
        m_rangeValid = false;
        emit dataChanged();
        emit pointRemoved(index);
    }
}

void PlotCurveData::updatePoint(int index, qreal x, qreal y)
{
    if (index >= 0 && index < m_data.size()) {
        m_data[index] = QPointF(x, y);
        m_rangeValid = false;
        emit dataChanged();
        emit pointUpdated(index);
    }
}

void PlotCurveData::updatePoint(int index, const QPointF &point)
{
    if (index >= 0 && index < m_data.size()) {
        m_data[index] = point;
        m_rangeValid = false;
        emit dataChanged();
        emit pointUpdated(index);
    }
}

void PlotCurveData::updateRange() const
{
    if (m_data.isEmpty()) {
        m_minX = m_maxX = m_minY = m_maxY = 0;
        m_rangeValid = true;
        return;
    }
    
    m_minX = m_maxX = m_data[0].x();
    m_minY = m_maxY = m_data[0].y();
    
    for (int i = 1; i < m_data.size(); ++i) {
        const QPointF &point = m_data[i];
        m_minX = qMin(m_minX, point.x());
        m_maxX = qMax(m_maxX, point.x());
        m_minY = qMin(m_minY, point.y());
        m_maxY = qMax(m_maxY, point.y());
    }
    
    m_rangeValid = true;
}

PlotCurveData::DataRange PlotCurveData::rangeByX(qreal minX, qreal maxX) const{
    DataRange result;
    if(m_data.isEmpty())
        return result;
    bool first = true;
    for(const QPointF &p : m_data){
        if(p.x() < minX || p.x() > maxX){
            continue;
        }
        if(first){
            result.minX = result.maxX = p.x();
            result.minY = result.maxY = p.y();
            first = false;
        }
        else{
            result.minX =qMin(result.minX, p.x());
            result.maxX = qMax(result.maxX, p.x());
            result.minY = qMin(result.minY, p.y());
            result.maxY = qMax(result.maxY, p.y());
        }
    }
    result.valid = !first;
    return result;
}

PlotCurveData::DataRange PlotCurveData::rangeByY(qreal minY, qreal maxY) const{
    DataRange result;
    if(m_data.isEmpty())
        return result;
    bool first = true;
    for(const QPointF &p : m_data){
        if(p.y() < minY || p.y() > maxY){
            continue;
        }
        if(first){
            result.minX = result.maxX = p.x();
            result.minY = result.maxY = p.y();
            first = false;
        }
        else{
            result.minX = qMin(result.minX, p.x());
            result.maxX = qMax(result.maxX, p.x());
            result.minY = qMin(result.minY, p.y());
            result.maxY = qMax(result.maxY, p.y());
        }
    }
    result.valid = !first;
    return result;
}

QVector<QPointF> PlotCurveData::pointsByX(qreal minX, qreal maxX) const
{
    QVector<QPointF> result;
    for (const auto &p : m_data)
    {
        if (p.x() >= minX && p.x() <= maxX)
            result.append(p);
    }
    return result;
}

QVector<QPointF> PlotCurveData::pointsByY(qreal minY, qreal maxY) const
{
    QVector<QPointF> result;
    for (const auto &p : m_data)
    {
        if (p.y() >= minY && p.y() <= maxY)
            result.append(p);
    }
    return result;
}

QVector<QPointF> PlotCurveData::pointsInRange(qreal minX, qreal maxX, qreal minY, qreal maxY) const
{
    QVector<QPointF> result;

    if (m_data.isEmpty())
        return result;

    int left = 0, right = m_data.size();

    // 第一个 >= minX
    while (left < right){
        int mid = (left + right) >> 1;
        if (m_data[mid].x() < minX)
            left = mid + 1;
        else
            right = mid;
    }

    int start = left;

    left = 0;
    right = m_data.size();

    // 第一个 > maxX
    while (left < right){
        int mid = (left + right) >> 1;
        if (m_data[mid].x() <= maxX)
            left = mid + 1;
        else
            right = mid;
    }

    int end = left;

    result.reserve(end - start);

    for (int i = start; i < end; ++i){
        const auto &p = m_data[i];
        if (p.y() >= minY && p.y() <= maxY)
            result.append(p);
    }

    return result;
}

// ==================== PlotCurveManager 实现 ====================

PlotCurveManager::PlotCurveManager(QObject *parent)
    : QObject(parent)
{
}

QSharedPointer<PlotCurveData> PlotCurveManager::addCurve(const QString &name)
{
    auto curve = QSharedPointer<PlotCurveData>::create(name);
    addCurve(curve);
    return curve;
}

void PlotCurveManager::addCurve(QSharedPointer<PlotCurveData> curve)
{
    if (curve && !m_curves.contains(curve)) {
        m_curves.append(curve);
        connect(curve.data(), &PlotCurveData::dataChanged,
                this, &PlotCurveManager::curvesChanged);
        connect(curve.data(), &PlotCurveData::appearanceChanged,
                this, &PlotCurveManager::curvesChanged);
        
        emit curvesChanged();
        emit curveAdded(m_curves.size() - 1);
    }
}

QSharedPointer<PlotCurveData> PlotCurveManager::curve(int index) const
{
    if (index >= 0 && index < m_curves.size()) {
        return m_curves[index];
    }
    return nullptr;
}

QSharedPointer<PlotCurveData> PlotCurveManager::curve(const QString &name) const
{
    for (const auto &curve : m_curves) {
        if (curve->name() == name) {
            return curve;
        }
    }
    return nullptr;
}

void PlotCurveManager::removeCurve(int index)
{
    if (index >= 0 && index < m_curves.size()) {
        auto curve = m_curves[index];
        disconnect(curve.data(), &PlotCurveData::dataChanged,
                   this, &PlotCurveManager::curvesChanged);
        disconnect(curve.data(), &PlotCurveData::appearanceChanged,
                   this, &PlotCurveManager::curvesChanged);
        
        m_curves.remove(index);
        emit curvesChanged();
        emit curveRemoved(index);
    }
}

void PlotCurveManager::removeCurve(const QString &name)
{
    int index = indexOf(name);
    if (index >= 0) {
        removeCurve(index);
    }
}

void PlotCurveManager::removeCurve(QSharedPointer<PlotCurveData> curve)
{
    int index = indexOf(curve);
    if (index >= 0) {
        removeCurve(index);
    }
}

void PlotCurveManager::clear()
{
    if (!m_curves.isEmpty()) {
        for (const auto &curve : m_curves) {
            disconnect(curve.data(), &PlotCurveData::dataChanged,
                       this, &PlotCurveManager::curvesChanged);
            disconnect(curve.data(), &PlotCurveData::appearanceChanged,
                       this, &PlotCurveManager::curvesChanged);
        }
        
        m_curves.clear();
        emit curvesChanged();
    }
}

int PlotCurveManager::indexOf(QSharedPointer<PlotCurveData> curve) const
{
    return m_curves.indexOf(curve);
}

int PlotCurveManager::indexOf(const QString &name) const
{
    for (int i = 0; i < m_curves.size(); ++i) {
        if (m_curves[i]->name() == name) {
            return i;
        }
    }
    return -1;
}

bool PlotCurveManager::getDataRange(qreal &minX, qreal &maxX, qreal &minY, qreal &maxY) const
{
    if (m_curves.isEmpty()) {
        return false;
    }
    
    bool first = true;
    for (const auto &curve : m_curves) {
        if (curve->isEmpty() || !curve->appearance()->visible()) {
            continue;
        }
        
        if (first) {
            minX = curve->minX();
            maxX = curve->maxX();
            minY = curve->minY();
            maxY = curve->maxY();
            first = false;
        } else {
            minX = qMin(minX, curve->minX());
            maxX = qMax(maxX, curve->maxX());
            minY = qMin(minY, curve->minY());
            maxY = qMax(maxY, curve->maxY());
        }
    }
    
    return !first;
}
