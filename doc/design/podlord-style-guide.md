# Podlord Design Style

## Direction

Modern Kubernetes operations UI with restrained late-1990s strategy-game influence. The interface should stay readable, dense, and professional; the retro layer comes through radar treatment, compact command controls, segmented indicators, and tactical icon plaques.

## Core Traits

- Subtle tactical console language
- Pixel-informed icons and radar markers
- Dark command-console surfaces with low noise
- Blue/cyan for system objects
- Amber for primary actions and hierarchy
- Red/orange only for danger states
- Near-black outlines
- Thin borders, rails, tabs, and slots
- Dense but readable controls

## Naming Theme

| Generic UI | Podlord Name |
|---|---|
| Dashboard | Command Center |
| Namespaces | Territories |
| Pods | Minions |
| Deployments | Orders |
| Logs | Battle Feed |
| Alerts | Sirens |
| Metrics | Intel |
| Settings | Armory |
| Cluster Map | War Room |

## Logo Usage

Use `src/Podlord.App/Assets/Brand/Logo/podlord-logo-transparent.png` on dark metal, royal blue glow, or muted earth backgrounds.

Avoid bright white backgrounds; the mark is designed for dark or muted surfaces.

Minimum practical sizes:

- App icon: 48px
- Header mark: 96px
- Splash/hero: 256px+

## Motion

- Hover: 100–140ms
- Panel open: 160–220ms
- Prefer stepped animations
- Avoid soft bounce animations
- Loading should use blinking LEDs, scanning bars, or mechanical flicker

## Implementation

Use `src/Podlord.App/Assets/Theme/podlord-tokens.json` as the native theme source and `doc/design/podlord-theme.css` as the CSS reference for any future web/docs previews. CSS classes included:

- `.pl-app-bg`
- `.pl-panel`
- `.pl-panel-title`
- `.pl-button`
- `.pl-button-primary`
- `.pl-button-danger`
- `.pl-resource`
- `.pl-led`
- `.pl-led-warning`
- `.pl-led-danger`
- `.pl-pixel-art`
- `.pl-flicker`
