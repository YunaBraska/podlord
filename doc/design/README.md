# Podlord Design Assets

Runtime assets:

- `src/Podlord.App/Assets/Brand/Logo/podlord-logo-transparent.png`
- `src/Podlord.App/Assets/Brand/Logo/podlord-logo-preview.png`
- `src/Podlord.App/Assets/Brand/Logo/podlord-logo-source.png`
- `src/Podlord.App/Assets/Brand/Icons/podlord-icon-*.png`
- `src/Podlord.App/Assets/Theme/podlord-tokens.json`

Design reference:

- `doc/design/podlord-style-guide.md`
- `doc/design/podlord-theme.css`

The Avalonia shell uses the `256px` app icon as the native window icon target and uses the transparent logo as a compact command mark. Keep new runtime images under `src/Podlord.App/Assets` so Avalonia can package them as resources.
