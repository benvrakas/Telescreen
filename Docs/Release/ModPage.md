# Telescreen

**Short description (SMR, at most 128 characters):**
Billboard screens for synced videos, a shared browser, shared windows and VM desktops that every player sees and hears.

---

Billboard-sized screens for Satisfactory. Put a screen up, paste a video link, and everyone near it watches the same
video at the same moment, with sound that comes from the screen. Or open a browser on it, share a window from your
PC, or show a virtual machine's desktop, and everyone watches that live.

**This is a beta.** It works in single player and on listen servers in our tests, but expect rough edges. Please
report problems with your logs (see below).

## Features

- **Small and Large Telescreen** (8 x 4 m and 16 x 8 m), built from the billboard models, unlocked in the AWESOME Shop
  (Signs, Telescreen Set, 25 FICSIT Coupons). Rescale them with a scaling mod and the picture follows.
- **Synced videos**: YouTube, Twitch, Vimeo and video files. A shared queue (everyone sees who added what), a seek
  bar, pause, next, 3D sound from the screen, letterbox / fill / stretch, screen names.
- **Shared built-in browser**: use it in a window with mouse and keyboard (web games that lock the mouse work).
  Everyone near the screen sees and hears it, streamed through the game's own connection: no ports to open.
- **Share a window** from your PC (a video player, a music app, a game) with its own sound. Only that window is
  shared, never your desktop.
- **Remote desktop (VNC)**: show a virtual machine's console (QEMU/KVM, Proxmox) or any VNC server on a screen, with
  mouse and keyboard in its window and the VM's sound (QEMU). Saved connections stay on your PC, passwords encrypted
  for your Windows user.
- **Let others help**: the player sharing can give a watching player the mouse, or mouse and typing, for a limited
  time. Viewers' pointers show on the screen, and anyone watching can click to point at something for everyone.
- **Sound through the game's audio engine**, like the game's own sounds: rooms, reverb and the game's volume sliders
  apply. Works alongside BoomBoxPlus. A per-screen **My volume** slider in each screen's panel.
- **Picture-in-picture** of any screen on your HUD, with sound that follows you. **F9** lists every screen.
- **Stream quality settings**: resolution, bitrate, frame rate and encoder (AV1, HEVC or H.264 on your graphics card,
  or AV1 in software), within the world host's limits.

## Who decides what

- **The world host** (the player hosting the world, or a dedicated server's admins) sets the rules for everyone in
  Mods > Telescreen > *Rules for this world*: who may change videos, which video sites are allowed, who may use the
  browser, share windows or show remote desktops (everyone / trusted players / only the host), how much control a
  viewer may be given, how many screens may be in use, and stream limits. The host can stop any screen's sharing.
- **The player sharing** (the browser, window or remote desktop runs on their PC) chooses what to share, can blank it
  for everyone at any time (**F10**), and decides who may control it, within the host's rules. Other players only get
  the picture and sound: never their other windows, their VNC addresses or passwords.

## Requirements

- SML 3.12 or later.
- **Windows** for playing videos and sharing (the helper programs are Windows-only). Dedicated servers (Windows or
  Linux) can run the mod; they only relay.
- Everyone in a multiplayer session needs the mod.
- Window sound needs Windows 11 or Windows 10 21H2 and later. Graphics card encoders: NVIDIA, AMD or Intel; without
  one, enable software AV1 (Mods > Telescreen > When you share > Stream encoder: 4).

## Good to know

- **The download is large (about 270 MB)** because the mod brings its own helper programs: a video player and encoder
  (FFmpeg), a link reader (yt-dlp with QuickJS) and a browser (Chromium Embedded Framework). They run only on your
  PC while the game runs, never on dedicated servers.
- **Antivirus programs may flag `TelescreenHost.exe`**: it is new, unsigned, opens a browser, captures a window you
  pick and downloads video. If it gets quarantined, allow it for the Telescreen mod folder.
- Videos are fetched by each player's own PC from the video site. Videos added by others load automatically only from
  sites on your allowed list (Mods > Telescreen > Privacy).
- Shared browsers, windows and desktops are visible to every player near the screen, including anything signed in.
  A notice explains this the first time. Plain VNC isn't encrypted: use it on your own network.
- What players show on screens comes from them and from third-party sites; Telescreen doesn't check it. Hosts can
  limit who may share.
- Downloading or streaming from YouTube may be against its terms of service; use it responsibly.

## Known limitations (beta)

- Streams go through the world host's connection; a slow host upload lowers quality (the stream adapts), and lost
  packets freeze the picture until the next full frame. Lower the stream bitrate if that happens often.
- The first video on a PC takes 5-10 seconds to start (link lookup). If YouTube links stop loading, the bundled yt-dlp
  may need an update (a mod update).
- Clicking the 3D screen directly isn't supported: use its panel and window.
- The browser's process sandbox is off in this beta. RDP isn't supported yet (VNC is).

## Reporting problems

Please attach `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log` (search for `LogTelescreen`),
`TelescreenHost.log` and `TelescreenBrowser.log` from the same folder, and say what you did, what you expected and
whether you were the host or a client.

## Credits and licences

Telescreen bundles, unmodified:

- [FFmpeg](https://ffmpeg.org) n8.1.3, LGPL v3, shared build from [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds)
  ([source](https://github.com/FFmpeg/FFmpeg/tree/n8.1.3)).
- [Chromium Embedded Framework](https://bitbucket.org/chromiumembedded/cef) 152 (BSD) with Chromium (its licences in
  `CREDITS-Chromium.html`).
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) (Unlicense) and [QuickJS-ng](https://github.com/quickjs-ng/quickjs) (MIT).
- [miniz](https://github.com/richgel999/miniz) (MIT), built into the helper.

Each component's licence and source notes are in the mod's `Resources/Runtime/<component>/` folders.

## AI disclosure

Written with the help of Claude (Anthropic).
