# Axionis Theme Bundle Format 1

This is the stable authoring contract for humans, tools and AI agents. A bundle
is a directory whose root contains `theme.ini`. Every bundled path is relative
to that root. Absolute paths, `..` escapes and symlinks are rejected.

## Canonical layout

Only `theme.ini` has a fixed filename. The numbered layout below is strongly
recommended because it makes prompts, diffs, reviews and generated bundles
predictable.

```text
neon-city/
├── theme.ini
├── preview.png
├── 1-background/
│   └── wallpaper.png
├── 2-cinnamon/
│   └── Axionis-Neon-Cinnamon/
│       └── cinnamon/
│           ├── cinnamon.css
│           ├── 2.1-panel.css
│           ├── 2.2-menu.css
│           ├── 2.3-tray.css
│           ├── 2.4-notifications.css
│           ├── 2.5-popovers.css
│           └── 2.6-osd.css
├── 3-applications/
│   └── Axionis-Neon-GTK/
│       └── gtk-3.0/
│           ├── gtk.css
│           ├── 3.1-controls.css
│           ├── 3.2-windows.css
│           └── 6-lock.css
├── 4-icons/
│   └── Axionis-Neon-Icons/
│       ├── index.theme
│       └── scalable/...
└── 5-cursor/
    └── Axionis-Neon-Cursor/
        ├── index.theme
        └── cursors/...
```

`cinnamon.css` and `gtk.css` should import the numbered CSS fragments they use.
The manager does not concatenate or rewrite CSS.

## Manifest

```ini
[Theme]
SchemaVersion=1
ID=neon-city
Name=Neon City
Description=Electric cyan and magenta on deep navy
Author=Example Author
Version=1.0
Preview=preview.png

[Components]
Wallpaper=1-background/wallpaper.png
CinnamonTheme=Axionis-Neon-Cinnamon
CinnamonPath=2-cinnamon/Axionis-Neon-Cinnamon
GtkTheme=Axionis-Neon-GTK
GtkPath=3-applications/Axionis-Neon-GTK
IconTheme=Axionis-Neon-Icons
IconPath=4-icons/Axionis-Neon-Icons
CursorTheme=Axionis-Neon-Cursor
CursorPath=5-cursor/Axionis-Neon-Cursor
CursorSize=24
ScreenLockStyled=true
LockCss=3-applications/Axionis-Neon-GTK/gtk-3.0/6-lock.css

[Appearance]
ColorScheme=prefer-dark
AccentRGB=#00E5FF
```

All keys except `SchemaVersion`, `ID`, and `Name` are optional. At least one
supported component must be present.

### `[Theme]`

| Key | Contract |
|---|---|
| `SchemaVersion` | Integer `1` |
| `ID` | Unique lowercase ASCII identifier: `a-z`, `0-9`, `_`, `-`; max 64 bytes |
| `Name` | Display name; max 128 bytes, no control characters |
| `Description` | Short display description |
| `Author` | Human or project name |
| `Version` | Bundle-defined version string |
| `Preview` | Relative PNG, JPEG, WebP or SVG preview path |

### `[Components]`

A `*Theme` value selects a theme name. Its optional matching `*Path` packages
that theme inside the bundle. Omit the path to reference a theme already
installed by Axionis. A packaged Cinnamon theme must have
`cinnamon/cinnamon.css`; packaged GTK themes must have `gtk-3.0/gtk.css`; icon
and cursor themes must have `index.theme`; cursor themes also need `cursors/`.
When a path is omitted, the named system theme must already be discoverable in
a standard XDG, system, or legacy per-user theme directory. Apply fails with a
specific error when that external reference is missing or incomplete.

`CursorSize` is an integer from 16 through 128.

When `ScreenLockStyled=true`, `GtkPath` and `LockCss` are required. `LockCss`
must be inside the GTK theme and contain `.csstage` selectors. The main
`gtk-3.0/gtk.css` must directly contain or import those rules, because Cinnamon
Screensaver examines the active GTK provider rather than a separate lock-theme
setting.

### `[Appearance]`

`ColorScheme` is `default`, `prefer-dark`, or `prefer-light`. It is a portal
preference for compatible applications—not brightness and not a replacement
for the actual GTK/Cinnamon colors.

`AccentRGB` uses `#RRGGBB`. Applications may ignore the preference. Theme CSS
should use the same accent explicitly so the bundle remains visually coherent.

## Numbered component map

The numbers are permanent within schema version 1. Top-level numbers are apply
units. Decimal numbers are authoring selectors used in prompts and documentation.

| Number | Stable ID | Meaning | Runtime apply unit |
|---:|---|---|---|
| 1 | `wallpaper` | Desktop background image | 1 |
| 2 | `desktop` | Complete Cinnamon shell stylesheet | 2 |
| 2.1 | `desktop.panel` | Panel colors, borders and states | 2 |
| 2.2 | `desktop.menu` | Menu, search and categories | 2 |
| 2.3 | `desktop.tray` | Tray/status containers | 2 |
| 2.4 | `desktop.notifications` | Notification banners/actions | 2 |
| 2.5 | `desktop.popovers` | Calendar and applet popovers | 2 |
| 2.6 | `desktop.osd` | OSD and Cinnamon dialogs | 2 |
| 3 | `applications` | Complete GTK 3/XApp stylesheet | 3 |
| 3.1 | `applications.controls` | Buttons, entries, lists, switches | 3 |
| 3.2 | `applications.windows` | Header/title bars, borders, shadows | 3 |
| 4 | `icons` | Application, symbolic and file-manager icons | 4 |
| 5 | `cursor` | Cursor artwork and logical size | 5 |
| 5.1 | `cursor.theme` | Cursor artwork | 5 |
| 5.2 | `cursor.size` | Logical cursor size | 5 |
| 6 | `lock` | Lock styling embedded in component 3 | 3 |
| 6.1 | `lock.stage` | Stage/overlay | 3 |
| 6.2 | `lock.unlock` | Password and unlock controls | 3 |
| 6.3 | `lock.status` | Clock, media and status | 3 |
| 7 | `appearance` | Portal appearance preferences | 7 |
| 7.1 | `appearance.mode` | Light/dark preference | 7 |
| 7.2 | `appearance.accent` | Preferred accent RGB | 7 |

The installed CLI is the machine-readable source of truth:

```sh
axionis-theme --map --json
```

## Prompt contract for agents

An agent should:

1. Copy an existing bundle or create the canonical numbered tree.
2. Interpret positive selectors as areas to create/change and exclusions as
   areas to preserve byte-for-byte from the chosen base.
3. Never add scripts, executable files, hooks, services, desktop entries,
   package-manager operations or absolute paths.
4. Preserve existing metrics and layout declarations—including padding,
   margins, minimum sizes and font sizes. The schema-1 Axionis profile changes
   colors/artwork, not panel or application geometry.
5. Make the portal accent/mode and actual CSS colors agree.
6. Ensure `cinnamon.css`/`gtk.css` import every intended numbered fragment.
7. Run `axionis-theme --validate BUNDLE` and fix all reported errors.
8. Present the bundle for user review/import; use `axionis-theme --import
   BUNDLE` only with the user's instruction, and do not apply it without the user's
   instruction.

Example request:

> From `axionis-base`, create `neon-city`. Change 1, 2, 3, 5 and 7, but preserve
> 2.2 and 2.3. Use cyan `#00E5FF` and magenta `#FF2BD6`; prefer dark mode.

This means the agent may edit the remaining files under components 2, 3, 5,
and 7, replaces component 1, and copies the base theme's menu/tray fragments
unchanged. Applying the result still loads component 2 as one Cinnamon theme.

## Import limits

An imported bundle may contain no more than 4,096 regular files and 256 MiB of
file data. Symlinks, special files, executable permission bits, and recognized
script/binary integration formats are rejected. See [SECURITY.md](SECURITY.md).
