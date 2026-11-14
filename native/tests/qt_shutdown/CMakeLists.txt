cmake_minimum_required(VERSION 3.25)
project(QtQuickShutdownCheck LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 6.11.3 EXACT REQUIRED COMPONENTS Core Gui Qml Quick)
add_executable(qt-quick-shutdown-test main.cpp)
target_link_libraries(qt-quick-shutdown-test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
enable_testing()
add_test(NAME qt.quick.application_owned_image_shutdown COMMAND qt-quick-shutdown-test)
get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
set_tests_properties(qt.quick.application_owned_image_shutdown PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${qt_prefix}/qml;QT_PLUGIN_PATH=${qt_prefix}/plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
