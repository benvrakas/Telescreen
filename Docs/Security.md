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
- **Encrypted remote desktops:** VeNCrypt (security type 19) with an X.509 certificate (subtypes X509None, X509Vnc,
  X509Plain) over Windows' TLS (SChannel, TLS 1.2/1.3). Windows' certificate store isn't used (VNC servers have their
  own certificates); the certificate's SHA-256 is pinned per address on the first connection (trust on first use,
  `VncCertificates.json`) and a different one is refused until the sharer presses "Trust the new certificate". The
  panel says whether a connection is encrypted. "Only connect to encrypted remote desktops" refuses servers without
  VeNCrypt. Plain VNC is still allowed by default (QEMU and most servers default to it).
- **VNC passwords:** VNC's own authentication (DES challenge) only uses the first 8 characters; the panel says so when
  a longer password is used. Inside TLS the challenge can't be watched; X509Plain (user name and password) sends the
  whole password inside TLS.
- **Control log:** every grant, revoke, expiry, take-back (F10, a new source) and the start and end of sharing is
  listed with its time (replicated, last 12, shown to the sharer and the world host). Giving control of a remote
  desktop asks every time; the other notices once.
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

## Direct connections

Off by default; both the sharer and the viewer must turn them on, and the world host can forbid them
(`AllowDirect`). See Docs/Architecture.md, Direct connections.

- **What the other side learns:** the addresses each PC offers (public address from STUN or UPnP, LAN addresses,
  127.0.0.1) go to the other player through the server. The world host's server sees them too, as it relays them.
- **Keys:** the server makes a random 128-bit key per link (from two GUIDs) and gives it only to the two players; it
  travels over the game connection. Every datagram is AES-128-GCM encrypted and authenticated with it (Windows CNG);
  the nonce is the direction and a per-direction counter, so it never repeats under one key. Datagrams that fail
  authentication are dropped, so nobody else can inject stream data or take over a link.
- **What goes over a link:** only the encoded stream (picture and sound) and probes; viewer input, grants and the
  curtain stay on the game connection and keep their server checks. The curtain stops the sender session, so
  nothing is sent directly either.
- **Ports:** the helper binds one UDP port (any, or the one set); with UPnP it asks the router to forward it for two
  hours at most and removes the mapping when the helper exits. Only packets of links the mod added are answered.
- **Outside services:** STUN (stun.l.google.com, stun.cloudflare.com) sees the PC's public address.

## Not done yet

CEF's process sandbox; anonymous TLS for VNC (TigerVNC's TLSVnc; Windows' TLS has no anonymous ciphers).
