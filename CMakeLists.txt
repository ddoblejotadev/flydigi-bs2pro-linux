cmake_minimum_required(VERSION 3.21)

project(flydigi-space VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)

qt_standard_project_setup()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(flydigi-space
    src/main.cpp
    src/backend.cpp
    src/backend.h
)

qt_add_qml_module(flydigi-space
    URI FlydigiSpace
    VERSION 1.0
    QML_FILES
        qml/Theme.qml
        qml/Main.qml
        qml/Gauge.qml
        qml/TempList.qml
        qml/Controls.qml
        qml/Pill.qml
)

target_link_libraries(flydigi-space PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

install(TARGETS flydigi-space RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/FlydigiSpace
        DESTINATION share/flydigi-space/qml)
install(FILES packaging/flydigi-space.desktop DESTINATION share/applications)
install(FILES packaging/flydigi-space.svg DESTINATION share/icons/hicolor/scalable/apps)
