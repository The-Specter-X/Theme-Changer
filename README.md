# Axionis Theme Manager

Axionis Theme Manager is a native C, GTK 3 and XApp application for applying a
coordinated Cinnamon desktop appearance from one declarative bundle. It is
tailored for Axionis, an LMDE-based Cinnamon distribution, but does not require
root privileges and does not run an AI model.

The GUI is for people. It provides both coordinated global bundles and a
component mixer for installed Cinnamon, GTK, icon and cursor themes. The
companion `axionis-theme` CLI and stable numbered component map are for
scripts and AI agents.

## Scope

Version 0.1 manages appearance only:

| Number | Component | Applied through |
|---:|---|---|
| 1 | Wallpaper | `org.cinnamon.desktop.background picture-uri` |
| 2 | Cinnamon desktop theme | `org.cinnamon.theme name` |
| 3 | GTK/XApp application theme | `org.cinnamon.desktop.interface gtk-theme` |
| 4 | Icon theme | `org.cinnamon.desktop.interface icon-theme` |
| 5 | Cursor theme and size | `org.cinnamon.desktop.interface cursor-theme`, `cursor-size` |
| 6 | Lock-screen styling | `.csstage` rules inside component 3's GTK CSS |
| 7 | Light/dark preference and accent | `org.x.apps.portal color-scheme`, `accent-rgb` |

It deliberately does not change panel placement, height, auto-hide, applets,
desktop layout, application opacity, terminal configuration, fonts, display
topology, multi-monitor wallpaper behavior, login backgrounds, or privileged
system files.

The program changes theme-name settings, not Cinnamon layout settings. CSS can
inherently affect visual padding or borders, so Axionis's authoring profile asks
theme creators and agents to preserve all non-color geometry unless a later
request explicitly expands the scope.

Light/dark is a preference advertised to applications that support the desktop
portal. It is not display brightness, and it does not recolor a GTK or Cinnamon
theme by itself.

## Graphical interface

The **Global Themes** page previews imported bundles and exposes checkboxes for
each component present in the selected bundle. A complete bundle can therefore
be applied as one coordinated theme, or only selected parts can be applied. The
page reports system themes referenced by a bundle that are missing before the
user presses Apply.

The **Components** page is the unified controller for the current desktop. It
discovers compatible themes from the standard user and system data locations.
Each theme control opens a visual grid and changes only the pending selection;
nothing is applied until **Apply Appearance** is pressed. GTK, Cinnamon and
cursor previews use the same per-theme and shared thumbnail conventions as
Cinnamon Settings. Icon themes render their own folder icon. Preview grids load
incrementally so large installed collections do not block application startup.

It provides controls for:

- Cinnamon desktop theme
- GTK 3/XApp application theme
- Icon theme
- Cursor theme and logical size
- Wallpaper with a local image preview
- Light/dark preference
- Optional accent preference
- Screen-lock styling status for the selected GTK theme

Applying a custom composition takes one undo snapshot. **Restore Previous**
restores every value changed by the most recent bundle or custom application.
The screen-lock row is informational because Cinnamon Screensaver consumes
`.csstage` rules from the selected GTK theme; it has no independent theme key.

## Storage

An imported bundle has one canonical location:

```text
$XDG_DATA_HOME/axionis-theme-manager/themes/<theme-id>/
```

That normally means
`~/.local/share/axionis-theme-manager/themes/<theme-id>/`. Theme artwork is
user data, so `.local/share` is the correct XDG location; `.config` is for
configuration. Apply/undo state is kept separately in
`$XDG_STATE_HOME/axionis-theme-manager/`, normally
`~/.local/state/axionis-theme-manager/`.

When a bundle carries a GTK/Cinnamon theme, icon theme, or cursor theme, the
manager exposes it with a symlink below `$XDG_DATA_HOME/themes` or
`$XDG_DATA_HOME/icons`. Cinnamon can discover it without duplicating any files.
Existing files or links are never overwritten. A referenced system theme is
checked before applying; missing or incomplete themes now produce an explicit
error instead of a silent desktop fallback.

## Build on Axionis/LMDE

```sh
sudo apt install build-essential meson ninja-build pkg-config \
  libglib2.0-dev libgtk-3-dev libxapp-dev
meson setup build --prefix=/usr
meson compile -C build
meson test -C build --print-errorlogs
sudo meson install -C build
```

Launch `Axionis Theme Manager` from Cinnamon's settings/application menu, or
run:

```sh
axionis-theme-manager
```

## Agent and command-line interface

```sh
# Inspect the stable human-readable or JSON component map
axionis-theme --map
axionis-theme --map --json

# Validate an agent-created bundle before importing it in the GUI
axionis-theme --validate ./neon-city --json
axionis-theme --import ./neon-city --json

# List installed bundles
axionis-theme --list --json

# Apply everything, or selected top-level components
axionis-theme --apply neon-city
axionis-theme --apply neon-city --only 1,2,3,5,7

# Undo the most recent apply operation
axionis-theme --restore
```

Subnumbers such as `2.2` and `6.1` are precise **authoring selectors**. They
tell an agent which CSS area to edit, but are not independent runtime settings:
Cinnamon loads component 2 as one stylesheet, and the lock screen consumes
component 3's GTK stylesheet. This keeps prompts precise without pretending the
desktop can atomically mix arbitrary sections from two CSS themes.

For example:

> Create a neon-city theme. Change 1, 2, 3, 5 and 7; keep 2.2 and 2.3 from the
> base theme. Validate the finished bundle with `axionis-theme --validate`.

See [THEME_FORMAT.md](THEME_FORMAT.md) for the complete authoring contract and
[CODE_OVERVIEW.md](CODE_OVERVIEW.md) for the code and data-flow maps.

## Security model

Bundles are declarative data. The importer rejects symbolic links, executable
permission bits, non-regular files, common executable/script formats, excessive
file counts, and bundles above 256 MiB. Asset paths must remain inside the
bundle. Applying uses GSettings APIs and file symlinks directly—there is no
shell execution, hook mechanism, package installation, or privilege prompt.

Read [SECURITY.md](SECURITY.md) for the trust boundary and reporting guidance.

## Upstream contracts

The implementation follows the upstream
[Cinnamon schemas](https://github.com/linuxmint/cinnamon/blob/master/data/org.cinnamon.gschema.xml),
[Cinnamon theme settings module](https://github.com/linuxmint/cinnamon/blob/master/files/usr/share/cinnamon/cinnamon-settings/modules/cs_themes.py),
[Cinnamon Screensaver theme detection](https://github.com/linuxmint/cinnamon-screensaver/blob/master/src/cinnamon-screensaver-main.py),
[XApp portal schema](https://github.com/linuxmint/xapp/blob/master/schemas/org.x.apps.gschema.xml),
and [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir/latest/).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
