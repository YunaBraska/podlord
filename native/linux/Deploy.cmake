# Opt-in packaging does not change development builds or other platforms.
include(GNUInstallDirs)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS SSL Crypto)
set(CMAKE_INSTALL_LIBDIR lib)
set_target_properties(podlord-native PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS podlord-native RUNTIME DESTINATION bin)
install(PROGRAMS "${CMAKE_CURRENT_LIST_DIR}/podlord" DESTINATION .)

qt6_generate_deploy_qml_app_script(
    TARGET podlord-native
    OUTPUT_SCRIPT podlord_linux_deploy
    NO_TRANSLATIONS
    INCLUDE_PLUGINS qwayland
    EXCLUDE_PLUGIN_TYPES sqldrivers styles
)
install(SCRIPT "${podlord_linux_deploy}")

# Qt's OpenSSL backend loads these by name, not necessarily through DT_NEEDED.
foreach(component IN ITEMS SSL CRYPTO)
    file(REAL_PATH "${OPENSSL_${component}_LIBRARY}" runtime)
    string(TOLOWER "${component}" soname)
    install(FILES "${runtime}" DESTINATION lib RENAME "lib${soname}.so.3")
endforeach()
install(CODE [[
    # The product uses Fusion and supports Basic. Other styles are not a runtime feature.
    foreach(style IN ITEMS Imagine Material Universal FluentWinUI3 iOS macOS Windows)
        file(REMOVE_RECURSE "${CMAKE_INSTALL_PREFIX}/qml/QtQuick/Controls/${style}")
        file(GLOB unused "${CMAKE_INSTALL_PREFIX}/lib/libQt6QuickControls2${style}*.so*")
        if(unused)
            file(REMOVE ${unused})
        endif()
    endforeach()
]])

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" DESTINATION licenses RENAME Podlord-LICENSE.txt)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/licenses/" DESTINATION licenses FILES_MATCHING PATTERN "*.txt")
get_filename_component(qt_prefix "${Qt6Core_DIR}/../../.." ABSOLUTE)
foreach(module IN ITEMS qtbase qtdeclarative qtmultimedia qtsvg qtwebsockets qtwayland)
    file(GLOB supplier_notices "${qt_prefix}/sbom/${module}-*.spdx.json")
    if(NOT supplier_notices)
        file(GLOB supplier_notices "${qt_prefix}/sbom/${module}-*.spdx")
        list(FILTER supplier_notices EXCLUDE REGEX "[.]source[.]spdx$")
    endif()
    if(NOT supplier_notices)
        message(FATAL_ERROR "Matching Qt supplier notices missing: ${module} under ${qt_prefix}")
    endif()
    install(FILES ${supplier_notices} DESTINATION licenses/qt-spdx)
endforeach()
