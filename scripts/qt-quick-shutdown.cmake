cmake_minimum_required(VERSION 3.25)

# Apply the unmodified upstream correction to already verified 6.11.3 sources,
# or rebuild Quick and its two plugins inside a caller-owned, fresh SDK prefix.
# Source mode: -DQT_SOURCE_DIR=/absolute/qtdeclarative
# SDK mode: -DQT_SDK_PREFIX=/absolute/qt -DWORK_ROOT=/absolute/new/work
# Android additionally requires QT_HOST_PATH, ANDROID_NDK_ROOT and ANDROID_ABI.
set(upstream_commit bbcaf3f39143b9add9722f4e31b51fc1dd102892)
set(patch_file "${CMAKE_CURRENT_LIST_DIR}/patches/qtdeclarative-6.11.3-shutdown.diff")
file(SHA256 "${patch_file}" patch_sha)
if(NOT patch_sha STREQUAL "82d657000b2b91e22a7ef59f21413d1ad73f96b0d0e3917eb597a1bed994d894")
    message(FATAL_ERROR "The pinned upstream Qt patch differs from its verified input.")
endif()
find_program(git_command git REQUIRED)
set(source_only FALSE)
if(DEFINED QT_SOURCE_DIR)
    if(DEFINED QT_SDK_PREFIX OR DEFINED WORK_ROOT OR NOT IS_ABSOLUTE "${QT_SOURCE_DIR}")
        message(FATAL_ERROR "Source-only mode requires only an absolute QT_SOURCE_DIR.")
    endif()
    set(source_only TRUE)
else()
    if(NOT IS_ABSOLUTE "${QT_SDK_PREFIX}" OR NOT IS_ABSOLUTE "${WORK_ROOT}"
        OR NOT IS_DIRECTORY "${QT_SDK_PREFIX}" OR EXISTS "${WORK_ROOT}" OR IS_SYMLINK "${WORK_ROOT}")
        message(FATAL_ERROR "SDK mode requires an existing absolute SDK prefix and a new absolute work directory.")
    endif()
    file(TO_CMAKE_PATH "${QT_SDK_PREFIX}" QT_SDK_PREFIX)
    file(MAKE_DIRECTORY "${WORK_ROOT}")
    find_program(curl_command NAMES curl curl.exe REQUIRED)
    execute_process(COMMAND "${curl_command}" --fail --location --silent --show-error
        --retry 2 --retry-all-errors --connect-timeout 20 --max-time 600
        "https://download.qt.io/archive/qt/6.11/6.11.3/submodules/qtdeclarative-everywhere-src-6.11.3.tar.xz"
        -o "${WORK_ROOT}/qtdeclarative.tar.xz" COMMAND_ERROR_IS_FATAL ANY)
    file(SHA256 "${WORK_ROOT}/qtdeclarative.tar.xz" archive_sha)
    if(NOT archive_sha STREQUAL "6d6db328a02ae3bb0b1e6944aeef8dfe44cd6b55ab59b1708bbf0400f5759cb5")
        message(FATAL_ERROR "The upstream Qt source archive differs from its pinned checksum.")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${WORK_ROOT}/qtdeclarative.tar.xz" DESTINATION "${WORK_ROOT}")
    set(QT_SOURCE_DIR "${WORK_ROOT}/qtdeclarative-everywhere-src-6.11.3")
endif()
file(STRINGS "${QT_SOURCE_DIR}/.cmake.conf" source_version REGEX "QT_REPO_MODULE_VERSION.*6[.]11[.]3")
if(NOT source_version)
    message(FATAL_ERROR "The shutdown backport is restricted to the pinned Qt 6.11.3 source.")
endif()
execute_process(COMMAND "${git_command}" apply --check "${patch_file}"
    WORKING_DIRECTORY "${QT_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${git_command}" apply "${patch_file}"
    WORKING_DIRECTORY "${QT_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${QT_SOURCE_DIR}/podlord-qtquick-patch.txt"
    "Qt Quick upstream shutdown fix: ${upstream_commit}\nQt Quick patch SHA256: ${patch_sha}\n")
if(source_only)
    return()
endif()

if(NOT DEFINED QT_GENERATOR)
    set(QT_GENERATOR Ninja)
endif()
if(NOT DEFINED QT_BUILD_JOBS)
    set(QT_BUILD_JOBS 4)
endif()
if(NOT QT_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "QT_BUILD_JOBS must be a positive integer.")
endif()
set(configure "${CMAKE_COMMAND}" -S "${QT_SOURCE_DIR}" -B "${WORK_ROOT}/build"
    -G "${QT_GENERATOR}" "-DCMAKE_PREFIX_PATH=${QT_SDK_PREFIX}"
    "-DCMAKE_INSTALL_PREFIX=${QT_SDK_PREFIX}" -DCMAKE_BUILD_TYPE=Release
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF)
if(DEFINED QT_HOST_PATH)
    if(NOT IS_DIRECTORY "${QT_HOST_PATH}" OR NOT IS_DIRECTORY "${ANDROID_NDK_ROOT}" OR NOT ANDROID_ABI)
        message(FATAL_ERROR "Android SDK rebuilding requires QT_HOST_PATH, ANDROID_NDK_ROOT and ANDROID_ABI.")
    endif()
    list(APPEND configure "-DCMAKE_TOOLCHAIN_FILE=${QT_SDK_PREFIX}/lib/cmake/Qt6/qt.toolchain.cmake"
        "-DQT_HOST_PATH=${QT_HOST_PATH}" "-DANDROID_NDK_ROOT=${ANDROID_NDK_ROOT}" "-DANDROID_ABI=${ANDROID_ABI}")
endif()
if(EXISTS "${QT_SDK_PREFIX}/lib/QtQuick.framework")
    execute_process(COMMAND ${configure} "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
        -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 COMMAND_ERROR_IS_FATAL ANY)
else()
    execute_process(COMMAND ${configure} COMMAND_ERROR_IS_FATAL ANY)
endif()
set(build_targets Quick qtquick2plugin quickwindow)
if(DEFINED QT_HOST_PATH)
    list(APPEND build_targets Qt6AndroidQuick)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK_ROOT}/build"
    --target ${build_targets} --config Release --parallel "${QT_BUILD_JOBS}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${WORK_ROOT}/build/src/quick" --config Release
    COMMAND_ERROR_IS_FATAL ANY)
file(COPY_FILE "${QT_SOURCE_DIR}/podlord-qtquick-patch.txt" "${WORK_ROOT}/qt-quick-fix.txt")
file(COPY_FILE "${QT_SOURCE_DIR}/podlord-qtquick-patch.txt" "${QT_SDK_PREFIX}/podlord-qtquick-patch.txt")
message(STATUS "Installed the pinned upstream Qt Quick shutdown correction.")
