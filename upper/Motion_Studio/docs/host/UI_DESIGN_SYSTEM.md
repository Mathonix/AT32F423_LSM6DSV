# UI Design System

## Direction

Visual references: Linear, GitHub Desktop, VS Code and Apple Pro Apps. The application should feel like an engineering workstation rather than a dashboard template.

## Core rules

- Dark and light themes.
- Dark background uses layered charcoal, not pure black.
- Accent is restrained teal and appears primarily on active/connected/focus states.
- Borders are low-contrast 1 px.
- Radius: 6–10 px, never oversized pill-card styling for main panels.
- Shadows are subtle and rare.
- No gradients, neon, glassmorphism or decorative particle effects.
- Information density is high but grouped by task.
- Numeric telemetry uses tabular/monospace presentation.
- Icons use Lucide only.

## Desktop workspace

- Sidebar: 196 px expanded, 56 px collapsed.
- Device bar: 48 px.
- Status bar: 26 px.
- Workspace fills remaining area.
- Main content uses split panes and toolbars instead of card grids.

## Navigation

1. Overview
2. Attitude
3. Signals
4. Calibration
5. CAN
6. Firmware
7. Diagnostics
8. Settings

Unsupported firmware areas remain visible because they document the platform roadmap, but controls are disabled and clearly labeled `Firmware not supported`.

## Typography

- UI: Inter / Segoe UI Variable / system UI.
- Telemetry: Cascadia Code / JetBrains Mono / monospace.
- Important values use tabular numerals.

## Motion

Framer Motion is limited to panel/tab/command-palette transitions and connection/progress feedback. No perpetual decorative animation.

## Accessibility

- keyboard focus states,
- `Ctrl+K` command palette,
- title/tooltips on icon-only controls,
- readable empty/disconnected/unsupported states,
- copyable diagnostics,
- text labels never rely on color alone.
