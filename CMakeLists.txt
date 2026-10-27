cmake_minimum_required(VERSION 3.21)

project(flydigi-space VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Widgets)

qt_standard_project_setup()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(flydigi-space
    src/main.cpp
    src/main.h
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
    Qt6::Widgets
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Network Test)
    qt_add_executable(backend_cooling_test
        tests/backend_cooling_test.cpp
        src/backend.cpp
        src/backend.h
    )
    target_include_directories(backend_cooling_test PRIVATE src)
    target_link_libraries(backend_cooling_test PRIVATE Qt6::Gui Qt6::Network Qt6::Test)
    add_test(NAME backend_cooling_test COMMAND backend_cooling_test)

    qt_add_executable(resident_lifecycle_test
        tests/resident_lifecycle_test.cpp
        src/main.h
        src/backend.cpp
        src/backend.h
    )
    qt_add_qml_module(resident_lifecycle_test
        URI FlydigiSpace
        VERSION 1.0
        RESOURCE_PREFIX /qt/qml
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/resident-lifecycle/FlydigiSpace
        QML_FILES qml/Theme.qml qml/Main.qml qml/Gauge.qml
                  qml/TempList.qml qml/Controls.qml qml/Pill.qml
    )
    target_include_directories(resident_lifecycle_test PRIVATE src)
    target_link_libraries(resident_lifecycle_test PRIVATE
        Qt6::Quick Qt6::Widgets Qt6::Network Qt6::Test)
    add_test(NAME resident_lifecycle_test COMMAND resident_lifecycle_test)
    set_tests_properties(resident_lifecycle_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 20
    )

    qt_add_executable(cooling_ui_test
        tests/cooling_ui_test.cpp
        src/backend.cpp
        src/backend.h
    )
    qt_add_qml_module(cooling_ui_test
        URI FlydigiSpace
        VERSION 1.0
        RESOURCE_PREFIX /qt/qml
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/cooling-ui/FlydigiSpace
        QML_FILES qml/Theme.qml qml/Controls.qml
    )
    target_include_directories(cooling_ui_test PRIVATE src)
    target_link_libraries(cooling_ui_test PRIVATE Qt6::Quick Qt6::Network Qt6::Test)
    add_test(NAME cooling_ui_test COMMAND cooling_ui_test)
    set_tests_properties(cooling_ui_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 20
    )
endif()

install(TARGETS flydigi-space RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/FlydigiSpace
        DESTINATION share/flydigi-space/qml)
install(FILES packaging/flydigi-space.desktop DESTINATION share/applications)
install(FILES packaging/flydigi-space.svg DESTINATION share/icons/hicolor/scalable/apps)
