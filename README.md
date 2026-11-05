# QtRhiPlot

[中文](#中文) | [English](#english)

License: MIT

## 中文

QtRhiPlot 是一个基于 Qt Quick 和 Qt RHI 的实时曲线绘图组件。组件在 QML 中以 `RhiPlot` 形式使用，支持多曲线、实时滚动、坐标轴、网格、鼠标缩放/拖拽、暂停后悬浮取值和 CSV 导出。

### 功能

- 基于 `QQuickItem` 的 QML 绘图组件
- 使用 Qt RHI 和 `.qsb` shader 渲染曲线、坐标轴和网格
- 支持多条曲线，每条曲线可设置颜色、宽度、线型和可见性
- 支持数值 X 轴和时间 X 轴
- 支持手动坐标范围和自动坐标范围
- 支持实时滚动 X 轴
- 支持鼠标滚轮缩放、左键拖拽平移
- 暂停跟随后支持悬浮窗显示各曲线插值
- 支持导出历史数据到 CSV
- 通过 `refreshRate` 限制界面刷新频率，数据写入可高于刷新频率

### 环境要求

- Qt 6.8 或更新版本
- CMake 3.16 或更新版本
- 支持 Qt 6 的 C++ 编译器
- Qt 模块：
  - `Core`
  - `Gui`
  - `GuiPrivate`
  - `Qml`
  - `Quick`
  - `ShaderTools`

注意：项目使用了 Qt 私有 RHI 头文件，例如 `<rhi/qrhi.h>`，因此需要 `GuiPrivate`。如果使用系统包管理器安装 Qt，需要安装对应版本的 Qt private development 包；如果使用 Qt 官方安装器，需要确保安装的 Qt 版本包含 private headers。

### 构建

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
cmake --build build --target QtRhiPlot
```

运行：

```bash
./build/QtRhiPlot
```

Qt Creator 用户可以直接打开 `CMakeLists.txt`，选择 Qt 6.8+ Kit 后构建。

### QML 基础用法

```qml
import QtQuick
import QtRhiPlotQML 1.0

Window {
    width: 800
    height: 480
    visible: true

    RhiPlot {
        id: plot
        anchors.fill: parent

        autoRange: false
        xMin: 0
        xMax: 8
        yMin: -1.5
        yMax: 1.5

        backgroundColor: "white"
        gridVisible: true
        axisVisible: true
        xAxisTitle: "Time (s)"
        yAxisTitle: "Value"
        xAxisLabelMode: RhiPlot.NumericXAxis

        Component.onCompleted: {
            addCurve("sensor")
            setCurveColor("sensor", "#0077b6")
            setCurveLineStyle("sensor", RhiPlot.SolidLine)

            addDataPoint("sensor", 0.0, 0.2)
            addDataPoint("sensor", 1.0, 0.8)
            addDataPoint("sensor", 2.0, 0.1)
        }
    }
}
```

### C++ 推送实时数据到 QML

推荐做法是：C++ 负责采集或生成数据，通过信号发送到 QML；QML 收到信号后调用 `RhiPlot` 的 `addDataPoint()`。

```cpp
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

    Q_INVOKABLE void start() { m_timer.start(); }
    Q_INVOKABLE void stop() { m_timer.stop(); }

    Q_INVOKABLE void reset()
    {
        m_startTime = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        m_currentTime = m_startTime;
        emit startTimeChanged();
    }

signals:
    void startTimeChanged();
    void sampleGenerated(double x, double y0, double y1);

private:
    void generateSample()
    {
        m_currentTime += m_timer.interval() / 1000.0;
        const double t = m_currentTime - m_startTime;
        emit sampleGenerated(m_currentTime, qSin(t * 2.6), qCos(t * 1.7));
    }

    QTimer m_timer;
    double m_startTime = 0.0;
    double m_currentTime = 0.0;
};
```

在 `main.cpp` 中暴露给 QML：

```cpp
QQmlApplicationEngine engine;
SampleFeeder sampleFeeder;
engine.rootContext()->setContextProperty("sampleFeeder", &sampleFeeder);
engine.loadFromModule("QtRhiPlotQML", "Main");
```

QML 接收信号：

```qml
RhiPlot {
    id: plot
    property real startTime: 0
    property real visibleSeconds: 8

    autoRange: false
    xAxisLabelMode: RhiPlot.TimeXAxis

    Component.onCompleted: {
        addCurve("sin")
        addCurve("cos")

        sampleFeeder.reset()
        startTime = sampleFeeder.startTime
        xMin = startTime
        xMax = startTime + 0.001
        sampleFeeder.start()
    }

    function appendSample(x, y0, y1) {
        addDataPoint(0, x, y0)
        addDataPoint(1, x, y1)

        if (followLatestX) {
            xMin = x - visibleSeconds
            xMax = x
        }
    }
}

Connections {
    target: sampleFeeder
    function onSampleGenerated(x, y0, y1) {
        plot.appendSample(x, y0, y1)
    }
}
```

1ms `QTimer` 不等于严格实时 1kHz。真实高频采集建议在 C++ 中缓存数据，并按较低 UI 频率批量发送给 QML。

### RhiPlot 属性

| 属性 | 类型 | 说明 |
| --- | --- | --- |
| `curveManager` | `PlotCurveManager` | 曲线管理器，通常不需要在 QML 中直接设置。 |
| `autoRange` | `bool` | 数据变化时自动调整坐标范围。 |
| `xMin`, `xMax` | `real` | 当前 X 轴可视范围。 |
| `yMin`, `yMax` | `real` | 当前 Y 轴可视范围。 |
| `backgroundColor` | `color` | 绘图背景色。 |
| `gridVisible` | `bool` | 是否显示网格。 |
| `gridColor` | `color` | 网格颜色。 |
| `axisVisible` | `bool` | 是否显示坐标轴、刻度、标签、标题和悬浮窗。 |
| `axisColor` | `color` | 坐标轴和刻度线颜色。 |
| `axisTextColor` | `color` | 坐标轴文字颜色。 |
| `plotLeftMargin` | `real` | 左侧绘图区边距。 |
| `plotRightMargin` | `real` | 右侧绘图区边距。 |
| `plotTopMargin` | `real` | 顶部绘图区边距。 |
| `plotBottomMargin` | `real` | 底部绘图区边距。 |
| `xAxisTitle` | `string` | X 轴标题。 |
| `yAxisTitle` | `string` | Y 轴标题。 |
| `zoomEnabled` | `bool` | 是否启用鼠标滚轮缩放。 |
| `followLatestX` | `bool` | 是否让 X 轴跟随最新数据。暂停后可查看历史并显示悬浮窗。 |
| `refreshRate` | `real` | 最大刷新率，单位 fps。设置为 `0` 表示立即刷新。 |
| `xAxisLabelMode` | `enum` | `RhiPlot.NumericXAxis` 或 `RhiPlot.TimeXAxis`。 |
| `hoverPopupEnabled` | `bool` | 是否启用内置悬浮窗。 |
| `hoverInfoValid` | `bool` | 悬浮信息是否有效，只读。 |
| `hoverX` | `real` | 当前悬浮 X 值，只读。 |
| `hoverPosition` | `point` | 当前悬浮位置，只读。 |
| `hoverValues` | `list` | 当前悬浮位置的各曲线值，只读。 |

### 枚举

线型：

```qml
RhiPlot.SolidLine
RhiPlot.DashLine
RhiPlot.DotLine
RhiPlot.DashDotLine
RhiPlot.DashDotDotLine
RhiPlot.NoLine
```

X 轴标签模式：

```qml
RhiPlot.NumericXAxis
RhiPlot.TimeXAxis
```

`TimeXAxis` 把 X 值当作 Unix epoch 秒，并显示为本地时间，例如 `HH:mm:ss.zzz`。

### QML 方法

曲线管理：

```qml
addCurve(name)
removeCurve(index)
removeCurve(name)
clearCurves()
curveCount()
getCurve(index)
getCurve(name)
```

数据操作：

```qml
addDataPoint(curveIndex, x, y)
addDataPoint(curveName, x, y)
setData(curveIndex, xData, yData)
setData(curveName, xData, yData)
clearHistoryData()
```

外观设置：

```qml
setCurveColor(curveIndex, color)
setCurveColor(curveName, color)
setCurveWidth(curveIndex, width)
setCurveWidth(curveName, width)
setCurveLineStyle(curveIndex, style)
setCurveLineStyle(curveName, style)
setCurveVisible(curveIndex, visible)
setCurveVisible(curveName, visible)
setCurveYScale(curveIndex, scale)
setCurveYScale(curveName, scale)
curveYScale(curveIndex)
curveYScale(curveName)
```

`setCurveYScale` 设置单条曲线的 Y 轴显示倍数（1.0 为原始比例）。
倍数只影响绘制，不修改原始数据，悬浮窗读数与 CSV 导出仍为真实值。
典型用法是配合图例色块的滚轮事件，实现类似 jScope 的通道缩放。

坐标和刷新：

```qml
autoAdjustRange()
updatePlot()
```

导出 CSV：

```qml
exportData(directoryPath, fileName)
exportDataFile(fileUrl)
```

### 鼠标交互

- 绘图区滚轮：缩放 X/Y 轴。
- X 轴区域滚轮：暂停跟随时缩放 X 轴。
- Y 轴区域滚轮：缩放 Y 轴。
- 左键拖拽：平移视图。暂停跟随后可以平移 X 轴。
- 暂停跟随后，鼠标移动到绘图区会显示悬浮窗，展示当前 X 位置各曲线的插值。

### 注意事项

- X 数据建议保持单调递增。可视范围优化和悬浮插值依赖 X 轴二分查找。
- `refreshRate` 只限制界面刷新，不限制数据写入。
- 高频数据建议在 C++ 侧批量缓存，避免 QML 每个点都处理一次。
- 当前项目使用 Qt 私有 API，升级 Qt 版本时需要同步验证 RHI 接口兼容性。

### 许可证

本项目使用 MIT License。详见 [LICENSE](LICENSE)。

## English

QtRhiPlot is a real-time Qt Quick plotting component rendered with Qt RHI. It is available in QML as `RhiPlot` from the `QtRhiPlotQML` module. It supports multiple curves, rolling time axes, grid and axis labels, mouse zoom and pan, paused hover inspection, and CSV export.

### Features

- QML plotting component based on `QQuickItem`
- Rendering through Qt RHI and `.qsb` shaders
- Multiple curves with independent color, width, line style, and visibility
- Numeric X axis and wall-clock time X axis
- Manual or automatic axis ranges
- Rolling X axis for real-time data
- Mouse wheel zoom and left-button panning
- Hover popup with interpolated values when following is paused
- CSV export for historical data
- Render throttling with `refreshRate`

### Requirements

- Qt 6.8 or newer
- CMake 3.16 or newer
- A C++ compiler supported by Qt 6
- Qt modules:
  - `Core`
  - `Gui`
  - `GuiPrivate`
  - `Qml`
  - `Quick`
  - `ShaderTools`

`GuiPrivate` is required because this project uses Qt private RHI headers such as `<rhi/qrhi.h>`.

### Build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
cmake --build build --target QtRhiPlot
```

Run:

```bash
./build/QtRhiPlot
```

### Basic QML Usage

```qml
import QtQuick
import QtRhiPlotQML 1.0

RhiPlot {
    id: plot
    anchors.fill: parent

    autoRange: false
    xMin: 0
    xMax: 8
    yMin: -1.5
    yMax: 1.5
    backgroundColor: "white"
    gridVisible: true
    axisVisible: true
    xAxisTitle: "Time (s)"
    yAxisTitle: "Value"

    Component.onCompleted: {
        addCurve("sensor")
        setCurveColor("sensor", "#0077b6")
        setCurveLineStyle("sensor", RhiPlot.SolidLine)
        addDataPoint("sensor", 0.0, 0.2)
        addDataPoint("sensor", 1.0, 0.8)
    }
}
```

### Real-Time Data From C++

Create a QObject data source in C++, expose it to QML, and append samples in QML when the signal arrives.

```cpp
QQmlApplicationEngine engine;
SampleFeeder sampleFeeder;
engine.rootContext()->setContextProperty("sampleFeeder", &sampleFeeder);
engine.loadFromModule("QtRhiPlotQML", "Main");
```

```qml
Connections {
    target: sampleFeeder
    function onSampleGenerated(x, y0, y1) {
        plot.addDataPoint(0, x, y0)
        plot.addDataPoint(1, x, y1)
    }
}
```

For high-frequency data sources, batch samples in C++ and send them to QML at a lower UI rate. A 1 ms `QTimer` is not guaranteed to run at exact 1 kHz on desktop systems.

### Main Properties

| Property | Type | Description |
| --- | --- | --- |
| `autoRange` | `bool` | Automatically fit axes to curve data. |
| `xMin`, `xMax` | `real` | Visible X axis range. |
| `yMin`, `yMax` | `real` | Visible Y axis range. |
| `backgroundColor` | `color` | Plot background color. |
| `gridVisible` | `bool` | Show or hide grid lines. |
| `gridColor` | `color` | Grid line color. |
| `axisVisible` | `bool` | Show or hide axes, labels, titles, and hover popup. |
| `axisColor` | `color` | Axis and tick line color. |
| `axisTextColor` | `color` | Axis text color. |
| `xAxisTitle` | `string` | X axis title. |
| `yAxisTitle` | `string` | Y axis title. |
| `zoomEnabled` | `bool` | Enable mouse wheel zoom. |
| `followLatestX` | `bool` | Keep X range following the latest data. Disable it to inspect history. |
| `refreshRate` | `real` | Maximum render rate in fps. Use `0` for immediate updates. |
| `xAxisLabelMode` | `enum` | `RhiPlot.NumericXAxis` or `RhiPlot.TimeXAxis`. |
| `hoverPopupEnabled` | `bool` | Enable the built-in hover popup. |

### Methods

Curve management:

```qml
addCurve(name)
removeCurve(index)
removeCurve(name)
clearCurves()
curveCount()
getCurve(index)
getCurve(name)
```

Data:

```qml
addDataPoint(curveIndex, x, y)
addDataPoint(curveName, x, y)
setData(curveIndex, xData, yData)
setData(curveName, xData, yData)
clearHistoryData()
```

Appearance:

```qml
setCurveColor(curveIndex, color)
setCurveColor(curveName, color)
setCurveWidth(curveIndex, width)
setCurveWidth(curveName, width)
setCurveLineStyle(curveIndex, style)
setCurveLineStyle(curveName, style)
setCurveVisible(curveIndex, visible)
setCurveVisible(curveName, visible)
setCurveYScale(curveIndex, scale)
setCurveYScale(curveName, scale)
curveYScale(curveIndex)
curveYScale(curveName)
```

`setCurveYScale` sets a per-curve Y-axis display multiplier (1.0 = original scale).
It only affects rendering; hover readouts and CSV export keep the real values.
A typical use is wiring it to a legend swatch wheel event for jScope-like channel scaling.

Export:

```qml
exportData(directoryPath, fileName)
exportDataFile(fileUrl)
```

### Enums

```qml
RhiPlot.SolidLine
RhiPlot.DashLine
RhiPlot.DotLine
RhiPlot.DashDotLine
RhiPlot.DashDotDotLine
RhiPlot.NoLine

RhiPlot.NumericXAxis
RhiPlot.TimeXAxis
```

`TimeXAxis` treats X values as Unix epoch seconds and formats labels as local time.

### Mouse Interaction

- Wheel over plot area: zoom X and Y.
- Wheel over X axis area: zoom X when `followLatestX` is false.
- Wheel over Y axis area: zoom Y.
- Left-button drag: pan the plot.
- Hover popup is available inside the plot area when `followLatestX` is false.

### Notes

- Keep X values monotonically increasing for range optimization and hover interpolation.
- `refreshRate` throttles rendering, not data ingestion.
- This project uses Qt private APIs. Re-test compatibility when upgrading Qt.

### License

This project is licensed under the MIT License. See [LICENSE](LICENSE).

## Project Layout

```text
.
├── CMakeLists.txt
├── LICENSE
├── README.md
├── main.cpp
├── Main.qml
└── PlotSrc
    ├── plotdata.cpp
    ├── plotdata.h
    ├── plot_rhi.frag
    ├── plot_rhi.vert
    ├── rhiplot.cpp
    └── rhiplot.h
```
