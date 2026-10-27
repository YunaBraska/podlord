#!/bin/sh
set -eu

test "$#" = 2

app_name='Podlord'
version=$1
build_number=$2
workspace_dir=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
dist_dir="${workspace_dir}/dist/app-store"
app_dir="${dist_dir}/${app_name}.app"
contents_dir="${app_dir}/Contents"
entitlements="${dist_dir}/${app_name}.entitlements"

: "${APP_STORE_BUNDLE_ID:?}"
: "${APP_STORE_TEAM_ID:?}"
: "${MACOS_APP_STORE_APPLICATION_IDENTITY:?}"
: "${MACOS_APP_STORE_INSTALLER_IDENTITY:?}"
: "${MACOS_APP_STORE_PROFILE_PATH:?}"

rm -rf "${dist_dir}"
PODLORD_APP_STORE=true \
BUNDLE_ID="${APP_STORE_BUNDLE_ID}" \
VERSION="${version}" \
CODESIGN_IDENTITY=skip \
sh "${workspace_dir}/scripts/build-macos-app.sh" macos-arm64

mkdir -p "${dist_dir}"
mv "${workspace_dir}/out/${app_name}.app" "${app_dir}"
cp "${MACOS_APP_STORE_PROFILE_PATH}" "${contents_dir}/embedded.provisionprofile"
/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier ${APP_STORE_BUNDLE_ID}" "${contents_dir}/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString ${version}" "${contents_dir}/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion ${build_number}" "${contents_dir}/Info.plist"
/usr/libexec/PlistBuddy -c 'Add :LSApplicationCategoryType string public.app-category.developer-tools' "${contents_dir}/Info.plist"

cp "${workspace_dir}/scripts/Podlord-AppStore.entitlements" "${entitlements}"
/usr/libexec/PlistBuddy -c "Add :com.apple.application-identifier string ${APP_STORE_TEAM_ID}.${APP_STORE_BUNDLE_ID}" "${entitlements}"
/usr/libexec/PlistBuddy -c "Add :com.apple.developer.team-identifier string ${APP_STORE_TEAM_ID}" "${entitlements}"
codesign --force --deep --entitlements "${entitlements}" --options runtime --timestamp --sign "${MACOS_APP_STORE_APPLICATION_IDENTITY}" "${app_dir}"
codesign --verify --deep --strict --verbose=2 "${app_dir}"
productbuild --component "${app_dir}" /Applications --sign "${MACOS_APP_STORE_INSTALLER_IDENTITY}" "${dist_dir}/${app_name}.pkg"
pkgutil --check-signature "${dist_dir}/${app_name}.pkg"
