import QtQuick
import QtQuick.Layouts 1.15
import Qt.labs.platform 1.1
import QtRhiPlotQML 1.0

Window {
    id: root
    width: 800
    height: 480
    visible: true // 必须设置为 true，否则窗口不会显示
    title: qsTr("RHI Plot Example")
    property string exportStatus: "" // Demo 导出状态提示文本

    RhiPlot {
        id: plot                           // 图表对象 id，供 Timer 和脚本调用
        anchors.fill: parent               // 让图表填满整个窗口
        autoRange: false                   // 是否根据曲线数据自动调整坐标范围；滚动刷新时手动控制 X 轴
        xMin: 0                            // 当前可视 X 轴最小值
        xMax: 8                            // 当前可视 X 轴最大值
        yMin: -1.5                         // 当前可视 Y 轴最小值
        yMax: 1.5                          // 当前可视 Y 轴最大值
        backgroundColor: "white"           // 图表背景色
        gridVisible: true                  // 是否显示网格线
        axisVisible: true                  // 是否显示坐标轴、刻度线和刻度文字
        xAxisTitle: "Time (s)"             // X 轴底部文字注释
        yAxisTitle: "Value"                // Y 轴左侧文字注释
        xAxisLabelMode: RhiPlot.TimeXAxis  // X 轴刻度显示模式：RhiPlot.NumericXAxis 显示数值，RhiPlot.TimeXAxis 按秒显示时间
        axisTextColor: "#20242a"           // 坐标轴刻度文字和轴标题颜色
        gridColor: "#d8dde6"               // 网格线颜色
        axisColor: "#20242a"               // 坐标轴和刻度线颜色
        zoomEnabled: true                  // 是否启用鼠标滚轮缩放：X 轴区域只缩放 X，Y 轴区域只缩放 Y，绘图区同时缩放 X/Y
        followLatestX: true                // 是否让 X 轴始终跟随最新数据；外部 UI 可以绑定这个属性做暂停/跟随控制
        refreshRate: 99                    // 图表最大刷新率，单位 fps；数据可以更高频写入，界面按此频率合并刷新，设置 0 表示立即刷新
        hoverPopupEnabled: true            // 是否启用组件内部悬浮窗；暂停跟随时显示当前 X 和各曲线 Y 值

        property real startTime: 0         // Demo 启动时的系统时间戳，单位秒
        property real visibleSeconds: 8    // 滚动窗口宽度，只显示最近 N 秒数据

        Component.onCompleted: {
            addCurve("solid")                                  // 添加第 1 条曲线：实线
            setCurveColor(0, "#0077b6")                        // 设置第 1 条曲线颜色
            setCurveLineStyle(0, RhiPlot.SolidLine)            // 设置第 1 条曲线线型为实线

            addCurve("dash")                                   // 添加第 2 条曲线：虚线
            setCurveColor(1, "#d00000")                        // 设置第 2 条曲线颜色
            setCurveLineStyle(1, RhiPlot.DashLine)             // 设置第 2 条曲线线型为虚线

            addCurve("dot")                                    // 添加第 3 条曲线：点线
            setCurveColor(2, "#2a9d8f")                        // 设置第 3 条曲线颜色
            setCurveLineStyle(2, RhiPlot.DotLine)              // 设置第 3 条曲线线型为点线

            addCurve("dashDot")                                // 添加第 4 条曲线：点划线
            setCurveColor(3, "#7b2cbf")                        // 设置第 4 条曲线颜色
            setCurveLineStyle(3, RhiPlot.DashDotLine)          // 设置第 4 条曲线线型为点划线

            addCurve("dashDotDot")                             // 添加第 5 条曲线：双点划线
            setCurveColor(4, "#f77f00")                        // 设置第 5 条曲线颜色
            setCurveLineStyle(4, RhiPlot.DashDotDotLine)       // 设置第 5 条曲线线型为双点划线

            sampleFeeder.reset()                               // 由 C++ 数据源生成时间和数据
            startTime = sampleFeeder.startTime                 // 获取 C++ 数据源的起始时间
            xMin = startTime - visibleSeconds                                // 初始化 X 轴左边界为当前时间
            xMax = startTime + 0.001                           // 初始给一个非零 X 轴范围
            sampleFeeder.start()                               // 曲线配置完成后再启动数据推送

        }

        function appendSample(x, y0, y1, y2, y3, y4) {
            addDataPoint(0, x, y0)                             // 向实线曲线追加一个点
            addDataPoint(1, x, y1)                             // 向虚线曲线追加一个点
            addDataPoint(2, x, y2)                             // 向点线曲线追加一个点
            addDataPoint(3, x, y3)                             // 向点划线曲线追加一个点
            addDataPoint(4, x, y4)                             // 向双点划线曲线追加一个点

            if (followLatestX) {                               // 跟随最新数据时才推动 X 轴窗口
                xMin = x - visibleSeconds                      // 数据超过窗口宽度后，按时间滚动
                xMax = x
            }
        }
    }

    Connections {
        target: sampleFeeder
        function onSampleGenerated(x, y0, y1, y2, y3, y4) {
            plot.appendSample(x, y0, y1, y2, y3, y4)
        }
    }

    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        spacing: 8

        Rectangle {
            width: 84
            height: 30
            radius: 4
            color: plot.followLatestX ? "#20242a" : "#ffffff"
            border.color: "#20242a"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: plot.followLatestX ? "Pause" : "Follow"
                color: plot.followLatestX ? "#ffffff" : "#20242a"
                font.pixelSize: 12
            }

            MouseArea {
                anchors.fill: parent
                onClicked: plot.followLatestX = !plot.followLatestX
            }
        }

        Rectangle {
            width: 72
            height: 30
            radius: 4
            color: "#ffffff"
            border.color: "#20242a"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "Clear"
                color: "#20242a"
                font.pixelSize: 12
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    plot.clearHistoryData()                         // 清除所有曲线的历史点，保留曲线配置、颜色和线型
                    sampleFeeder.reset()                            // C++ 数据源重新设置时间基准
                    plot.startTime = sampleFeeder.startTime
                    plot.xMin = plot.startTime
                    plot.xMax = plot.startTime + 0.001
                    root.exportStatus = "History cleared"
                }
            }
        }

        Rectangle {
            width: 76
            height: 30
            radius: 4
            color: "#ffffff"
            border.color: "#20242a"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "Export"
                color: "#20242a"
                font.pixelSize: 12
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    exportDialog.open() // 弹出系统文件选择框，由用户选择历史数据导出位置
                }
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: "Export history data"
        fileMode: FileDialog.SaveFile
        nameFilters: ["CSV files (*.csv)", "All files (*)"]
        folder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        currentFile: StandardPaths.writableLocation(StandardPaths.DownloadLocation) + "/HistoryData.csv"
        onAccepted: {
            var ok = plot.exportDataFile(currentFile) // 直接把 FileDialog 的本地文件 URL 传给库，库内处理跨平台路径
            root.exportStatus = ok ? ("Exported: " + currentFile) : "Export failed"
        }

        onRejected: {
            root.exportStatus = "Export canceled"
        }
    }
}
