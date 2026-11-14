# Configure the real native app with an isolated, test-only Android Application.
# cmake -S native -B BUILD -DCMAKE_PROJECT_INCLUDE=ABSOLUTE_PATH_TO_THIS_FILE ...
if(NOT ANDROID)
    message(FATAL_ERROR "The startup lifecycle regression requires the Android toolchain.")
endif()
get_filename_component(startup_repo "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(podlord_startup_package "${CMAKE_BINARY_DIR}/startup-package")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/PodlordStartupApplication.java")
file(MAKE_DIRECTORY "${podlord_startup_package}")
file(COPY "${startup_repo}/native/android/" DESTINATION "${podlord_startup_package}")
file(READ "${podlord_startup_package}/AndroidManifest.xml" startup_manifest)
string(REPLACE "org.qtproject.qt.android.bindings.QtApplication"
    "org.qtproject.qt.android.PodlordStartupApplication" startup_manifest "${startup_manifest}")
file(WRITE "${podlord_startup_package}/AndroidManifest.xml" "${startup_manifest}")
file(MAKE_DIRECTORY "${podlord_startup_package}/src/org/qtproject/qt/android")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/PodlordStartupApplication.java"
    DESTINATION "${podlord_startup_package}/src/org/qtproject/qt/android")
cmake_language(DEFER CALL set_property TARGET podlord-native PROPERTY
    QT_ANDROID_PACKAGE_SOURCE_DIR "${podlord_startup_package}")
