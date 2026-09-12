# Security policy

## Design boundary

Axionis Theme Manager treats imported themes as untrusted declarative data.
It never executes a theme-provided command and the bundle format has no hooks,
plugins, scripts, service definitions or package-install instructions. Applying
uses GLib/GSettings and `symlink(2)` directly; no command string is sent to a
shell and no root authorization is requested.

The importer rejects:

- paths that are absolute or resolve outside the bundle;
- symbolic links, devices, sockets, FIFOs and other non-regular content;
- any file carrying a user/group/other executable bit;
- common script, shared-library, desktop/service and policy suffixes;
- more than 4,096 files or more than 256 MiB of file data;
- malformed or unsupported manifests and required asset-layout failures.

Existing destinations below `$XDG_DATA_HOME/themes` and
`$XDG_DATA_HOME/icons` are never overwritten.

## Remaining trust boundary

GTK, Cinnamon, icon, cursor and image loaders still parse the imported CSS and
artwork. File-format parsers can have vulnerabilities, so users should install
themes from sources they trust and keep Axionis security updates installed.
The importer limits exposure but is not a general malware scanner.

The built-in system bundle directory is part of the Axionis package trust
boundary. User bundles override equal system IDs by XDG precedence.

## Reporting

Do not publish a suspected vulnerability before maintainers have had a chance
to investigate. Open a private GitHub security advisory for the repository and
include the affected version, reproduction bundle, expected behavior, observed
behavior and impact. Do not include personal data or secrets.

## Supported versions

Until the first stable Axionis release, security fixes are made on the current
`main` branch.
