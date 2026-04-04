# Changelog

## 2026.4.4

### Added

- **System tray icon** management via `setIcon()` for Windows (`.ico`), macOS (`.png` via base64), and Linux (`.png` with AppIndicator icon theme path).
- **Tooltip** support via `setToolTip()` (no-op on Linux where AppIndicator has no tooltip API).
- **Context menu** builder with four item types:
  - `TrayMenuItem` — normal clickable item
  - `TrayMenuItem.separator()` — visual divider
  - `TrayMenuItem.checkbox()` — toggle item with checked state
  - `TrayMenuItem.submenu()` — nested sub-menu with child items
- **Auto-incremented IDs** for menu items — no external ID generator needed.
- **Event listener** mixin (`DesktopTrayListener`) for:
  - Left / right mouse button press and release on the tray icon
  - Menu item click with automatic ID → `TrayMenuItem` resolution
- **TrayMenu** utility class with `findByKey()` and `findById()` lookups.
- **Linux crash prevention**: deferred `GtkMenuItem` destruction via `g_idle_add()` to avoid libdbusmenu heap corruption.
- **Sandbox detection** for Flatpak, Snap, Docker, and Podman environments.

