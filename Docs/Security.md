# Security and privacy: who decides what

Telescreen has two kinds of "host", and they protect different things. The UI and these docs never say just "host".

| | **World host** | **Sharer** (the screen's controller) |
|---|---|---|
| Who | The player hosting the world (listen server), or on a dedicated server its admins (`AFGPlayerState::IsServerAdmin`) and the server's own settings | The player whose browser, window or remote desktop a screen shows. It runs on **their PC** |
| Protects | The world and its players: who may share what, bandwidth, spam | Their own PC, accounts and privacy |
| Where it's set | Mods menu, Telescreen, **Rules for this world (world host)** (applies when this PC hosts; a dedicated server uses its own copy) | Mods menu, Telescreen, **When you share (your PC)**, plus the panel and keys while sharing |
| Enforced by | The server (`UTSRemoteCallObject`, `ATSBuildableScreen`) | The sharer's own client and helper |

The world host's rules are a ceiling: a sharer can always do less than the world allows, never more.

## World host (rules for everyone)

Checked on the server for every request; a refused request gets a notice ("Only the world host can...").

- **Who may use each mode** (`BrowserRole`, `WindowRole`, `RemoteDesktopRole`): everyone, trusted players (a name
  list) and the world host, or only the world host and server admins. Defaults: browser everyone, window trusted,
  remote desktop world host only.
- **Most control a viewer can get** per mode (`ViewerInputBrowser/Window/Remote`): nothing, mouse, mouse and typing,
  everything (browser only). Defaults: browser mouse, window and remote desktop nothing. A sharer's grant is clamped
  to this on the server.
- **Sharing on/off and bandwidth** (`AllowStreaming`, `MaxStreamKbps`): the server drops stream data above the cap.
- **Take-over** (`AllowTakeOver`) and **screens in use at once** (`MaxControlledScreens`); each player controls one
  screen at a time.
- **Videos:** who may change them (`QueuePolicy`), allowed sites, longest video, adds per minute.
- **Tools:** the world host and server admins see **Stop it (world host)** on any screen someone else uses.
- The server also rate-limits viewer input (120 events a second per viewer), pings (3 a second), keyframe requests
  (4 a second per screen), and validates every message (a failed validation disconnects, so only messages the mod
  never sends fail it).

## Sharer (their own PC)

Whatever a screen shows from a player's PC runs there: the built-in browser (with its logins, if kept), a window, a
remote desktop. Nothing of it leaves the PC except the encoded picture and sound.

- **Notices before sharing:** the first time a player opens the browser, shares a window, connects a remote desktop
  or gives a viewer control, a dialog explains what others will see or be able to do; they confirm once.
- **What others see:** a red line on the sharer's HUD while sharing ("Sharing ... : N watching"), the viewer count in
  the panel and window title.
- **Privacy curtain** (F10, or Blank for everyone): stops the stream for everyone while the sharer keeps using the
  source. It is also the **panic key**: turning it on revokes every viewer's control.
- **Giving control** (viewer input grants): only to named viewers who are watching, scope mouse / mouse and typing /
  everything (browser), for a set time (default 15 minutes, a sharer setting), revocable at any time. The sharer's
  client filters every relayed event again:
  - mouse only: no keys at all;
  - typing: printable characters and Enter, Backspace, Tab, arrows, Home/End, Delete; no Ctrl or Alt combinations;
  - everything: all keys except Windows keys, Alt+Tab, Alt+F4, Alt+D, F6, F11, F12, Ctrl+W/T/N/L/S/P/O,
    Ctrl+C/X/V and Ctrl/Shift+Insert (the clipboard), Ctrl+Shift+I/J (developer tools). Pointer lock deltas never come
    from viewers.
- **Secrets stay local:** remote desktop addresses and passwords never replicate (the screen carries a random token);
  saved connections encrypt passwords with Windows DPAPI for the current user. Window titles of the picker never leave
  the PC.
- **Browser hardening** (whoever is in control): `file://`, `chrome://` and other internal pages blocked, downloads
  and external protocol handlers refused, every permission prompt (camera, microphone, location, notifications,
  clipboard, USB...) denied, a clean profile unless the sharer keeps logins.

## Viewers (watching)

- Viewers only receive pictures and sound through the server; they never fetch anything for a stream. "Watch other
  players' browsers and windows" (Privacy) turns that off on their PC.
- Without control, a viewer's click in the screen window is a **ping**: a named marker everyone watching sees for a
  few seconds. With control, the screen shows their named cursor to everyone watching.

## Not done yet

CEF's process sandbox; VeNCrypt (TLS) for VNC; per-grant logs in the panel (the server log has every grant and
revoke).
