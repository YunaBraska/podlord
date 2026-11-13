# Keep each statically linked vendor's original declaration in a resource bundle.
# Framework manifests remain in their SDK for downstream consumers as well.
function(podlord_add_ios_privacy target)
    get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    set(vendors QtCore QtGui OpenSSL)
    set(sources
        "${qt_prefix}/lib/QtCore.framework/PrivacyInfo.xcprivacy"
        "${qt_prefix}/lib/QtGui.framework/PrivacyInfo.xcprivacy"
        "${OPENSSL_ROOT_DIR}/share/podlord/PrivacyInfo.xcprivacy")
    set(app_manifest "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PrivacyInfo.xcprivacy")
    target_sources(${target} PRIVATE "${app_manifest}")
    set_source_files_properties("${app_manifest}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    foreach(vendor source IN ZIP_LISTS vendors sources)
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "Missing ${vendor} privacy metadata. Prepare this SDK with scripts/install-native-ios-privacy.cmake.")
        endif()
        set(bundle "${CMAKE_CURRENT_BINARY_DIR}/privacy/${vendor}Privacy.bundle")
        file(MAKE_DIRECTORY "${bundle}")
        configure_file("${source}" "${bundle}/PrivacyInfo.xcprivacy" COPYONLY)
        file(CONFIGURE OUTPUT "${bundle}/Info.plist" CONTENT "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<plist version=\"1.0\"><dict>
<key>CFBundleIdentifier</key><string>berlin.yuna.podlord.privacy.@vendor@</string>
<key>CFBundlePackageType</key><string>BNDL</string>
<key>CFBundleVersion</key><string>1</string>
</dict></plist>" @ONLY)
        target_sources(${target} PRIVATE "${bundle}/PrivacyInfo.xcprivacy" "${bundle}/Info.plist")
        set_source_files_properties("${bundle}/PrivacyInfo.xcprivacy" "${bundle}/Info.plist"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/${vendor}Privacy.bundle")
    endforeach()
endfunction()
