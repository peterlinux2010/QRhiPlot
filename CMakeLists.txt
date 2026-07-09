cmake_minimum_required(VERSION 3.16)

project(QtRhiPlot VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_NO_PRIVATE_MODULE_WARNING ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick ShaderTools)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(QtRhiPlot
    main.cpp
    PlotSrc/rhiplot.cpp PlotSrc/rhiplot.h
)

target_include_directories(QtRhiPlot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/PlotSrc
)

qt_add_qml_module(QtRhiPlot
    URI QtRhiPlotQML
    VERSION 1.0
    QML_FILES
        Main.qml
        SOURCES PlotSrc/plotdata.cpp PlotSrc/plotdata.h
    # RESOURCE_PREFIX /scenegraph/rhiplotqml
    # NO_RESOURCE_TARGET_PATH
)

qt_add_shaders(QtRhiPlot "rhiplotqml_shaders"
    PREFIX /scenegraph/rhiplotqml
    BASE PlotSrc
    FILES
        PlotSrc/plot_rhi.vert
        PlotSrc/plot_rhi.frag
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(QtRhiPlot PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appQtRhiPlot
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(QtRhiPlot
    PRIVATE Qt6::Quick
    Qt6::GuiPrivate
    Qt6::Qml
    Qt6::Quick
)

include(GNUInstallDirs)
install(TARGETS QtRhiPlot
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
