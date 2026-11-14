# PodLord distribution channels

This document answers one question: what does each delivery channel do, and
what must exist before it can publish?

## Shared CI/CD policy

Every pull request and merge to `main` runs the native qualification workflow.
A merge is a snapshot check only. It creates neither a tag nor a GitHub Release.
The weekly release dispatcher can start `release.yml` only when production
sources changed since the latest stable tag. The manual release workflow can
also be forced for a deliberate unchanged release.

The stable workflow resolves one three-part semantic version from the latest
reachable stable tag. It passes that version to all native packages. It reuses
only artifacts created for the same commit. A failed qualification, package,
signature or notarization stops before tag and GitHub Release creation.

## GitHub Releases and direct downloads

The stable workflow creates the GitHub Release only after native qualification.
It publishes Windows, Linux, and two direct macOS assets per architecture:
`podlord-native-macos-arm64.zip`, `podlord-native-macos-arm64.dmg`,
`podlord-native-macos-x64.zip`, and `podlord-native-macos-x64.dmg`.

The macOS ZIP and DMG contain an app signed with the existing Developer ID
certificate. CI submits the app to Apple notarization, staples the accepted
ticket, then verifies its signature and Gatekeeper assessment. Required
repository secrets already exist:

- `MACOS_SIGNING_CERTIFICATE`
- `MACOS_SIGNING_CERTIFICATE_PASSWORD`
- `MACOS_NOTARY_ISSUER_ID`
- `MACOS_NOTARY_KEY`
- `MACOS_NOTARY_KEY_ID`

The first hosted stable workflow run is the remaining proof for this path. It
is not safe to infer a successful notarization from local configuration.

## Mac App Store

The same stable workflow prepares a Universal2 signed `Podlord.pkg`. It uses
the stored App Store application certificate, installer certificate and
provisioning profile. The Store sandbox requests only selected-file read access
and outbound network access. It does not request incoming network access.

`app-store-upload.yml` accepts only the `app-store-package` artifact from a
successful package or stable-release run. Upload is intentionally separate from
the GitHub release because it creates an external App Store Connect delivery.
It does not submit App Review.

Required repository secrets already exist:

- `MACOS_APP_STORE_APPLICATION_CERTIFICATE`
- `MACOS_APP_STORE_CERTIFICATE_PASSWORD`
- `MACOS_APP_STORE_INSTALLER_CERTIFICATE`
- `MACOS_APP_STORE_PROFILE`

## Google Play

PodLord already uses the permanent Android package identifier
`berlin.yuna.podlord`. Google Play requires a Play Console developer account,
an app record with that exact package identifier, Play App Signing, a signed
Android App Bundle, and a strictly increasing Android `versionCode`. None of
those publisher credentials are stored in this repository today.

Before CI can upload, create the Play app and store only these release-specific
values as repository secrets:

- `ANDROID_UPLOAD_KEYSTORE`
- `ANDROID_UPLOAD_KEYSTORE_PASSWORD`
- `ANDROID_UPLOAD_KEY_ALIAS`
- `ANDROID_UPLOAD_KEY_PASSWORD`
- `GOOGLE_PLAY_SERVICE_ACCOUNT_JSON`

The service account must have release access only to PodLord. The first target
should be internal testing. If the Play developer account is personal and was
created after 2023-11-13, Google requires a closed test with 12 opted-in testers
for 14 days before production access.

No upload workflow is installed yet because a workflow without the Play account,
app record, signing key, and service account would only create permanent failed
runs. The Android build remains available through the pinned native Android
instructions in `native/README.md`.

## Setapp

The direct macOS output provides the required Developer ID signing,
notarization, arm64/x86_64 support and a clean ZIP. It is not a Setapp archive.
Setapp additionally requires developer acceptance, an approved bundle
identifier, its update security policy and icon layout, plus Framework or
Vendor API integration. It also forbids the direct channel's proprietary
updater and licensing behavior.

Do not upload a direct GitHub package to Setapp. After Setapp accepts PodLord,
add their assigned identifier and integration settings as repository secrets,
then add a separate signed Setapp build. Its first upload must follow the
Setapp portal flow. This keeps the Store, direct, and Setapp channels honest
about their different update and entitlement rules.
