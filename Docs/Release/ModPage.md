<!--
Text for the Telescreen page on ficsit.app (Satisfactory Mod Repository).
- "Short description" field: the line under SHORT DESCRIPTION (at most 128 characters).
- "Full description" field: everything under FULL DESCRIPTION.
- The two transparency fields: the text under NETWORK ACTIVITY TRANSPARENCY and AI USAGE TRANSPARENCY.
-->

# SHORT DESCRIPTION

Billboard screens for synced videos, a shared browser, shared windows and VM desktops that every player sees and hears.

# FULL DESCRIPTION

Billboard-sized screens for Satisfactory. Put a screen up, paste a video link, and everyone near it watches the same
video at the same moment, with sound that comes from the screen. Or open a browser on it, share a window from your
PC, or show a virtual machine's desktop, and everyone watches that live.

![A YouTube video on a Large Telescreen, seen from the factory](https://raw.githubusercontent.com/benvrakas/Telescreen/main/Docs/Images/screen-video.jpg)

**Multiplayer hasn't been tested yet.** Everything below is built for multiplayer (synced videos, streams to other
players, direct connections, viewer control, the world host's rules), but so far it has only been tried in single
player. If you play it with friends, please tell me how it went, with your logs (see "Reporting problems").

## Features

- **Small and Large Telescreen** (8 x 4 m and 16 x 8 m), built from the billboard models, unlocked in the AWESOME Shop
  (Signs, Telescreen Set, 25 FICSIT Coupons). Rescale them with a scaling mod and the picture follows.
- **Synced videos**: YouTube, Twitch, Vimeo and video files. A shared queue (everyone sees who added what), a seek
  bar, pause, next, 3D sound from the screen, letterbox / fill / stretch, screen names.
- **Your own video files** (e.g. your Plex library): pick a file, and every player plays their own copy, found by
  file name in their videos folder even when it's somewhere else on their PC. Players who don't have it, or whose PC
  can't play a link, watch it streamed from a player who plays it.
- **Subtitles** on the screen, on your HUD, both or off (your choice, on your PC): a site's subtitles and YouTube's
  automatic captions, text tracks inside video files, or a .srt / .vtt file next to a video. Pick the track in the
  Videos tab; your preferred language is picked first.
- **Shared built-in browser**: use it in a window with mouse and keyboard (web games that lock the mouse work).
  Everyone near the screen sees and hears it, streamed through the game's own connection (no ports to open), or
  over a direct connection if you turn those on.
- **Share a window** from your PC (a video player, a music app, a game) with its own sound. Only that window is
  shared, never your desktop.
- **Remote desktop (VNC)**: show a virtual machine's console (QEMU/KVM, Proxmox) or any VNC server on a screen, with
  mouse and keyboard in its window and the VM's sound (QEMU). **Encrypted** (TLS) with servers that offer VeNCrypt with
  a certificate; the certificate is remembered on the first connection and a different one is refused. Saved
  connections stay on your PC, passwords encrypted for your Windows user.
- **Let others help**: the player sharing can give a watching player the mouse, or mouse and typing, for a limited
  time. Viewers' pointers show on the screen, and anyone watching can click to point at something for everyone. A
  control log in the panel lists who got control and when; control of a remote computer is confirmed every time.
- **Sound through the game's audio engine**, like the game's own sounds: rooms, reverb and the game's volume sliders
  apply. Works alongside BoomBoxPlus. Each screen has a volume for everyone and your own **My volume**.
- **Picture-in-picture** of any screen on your HUD, with sound that follows you. **F9** lists every screen.
- **Stream quality settings**: resolution, bitrate, frame rate and encoder (AV1, HEVC or H.264 on your graphics card,
  or AV1 in software), within the world host's limits.
- **Direct connections** (optional): live streams go straight between the sharing player's PC and each viewer's,
  encrypted, instead of through the world host, so the host's upload isn't the limit. Works through most home routers;
  can open the port with UPnP, or use one you forward. Falls back to the world host's connection by itself.

## Screenshots

![The screen panel: the screen's picture, volumes and settings on the left; Videos, Browser, Share a window and Remote desktop tabs on the right](https://raw.githubusercontent.com/benvrakas/Telescreen/main/Docs/Images/panel-videos.jpg)

![The built-in browser on a screen, searching the web](https://raw.githubusercontent.com/benvrakas/Telescreen/main/Docs/Images/screen-browser.jpg)

![A Debian virtual machine's desktop on a screen over VNC](https://raw.githubusercontent.com/benvrakas/Telescreen/main/Docs/Images/screen-vm.jpg)

## Who decides what

- **The world host** (the player hosting the world, or a dedicated server's admins) sets the rules for everyone in
  Mods > Telescreen > *Rules for this world*: who may change videos, which video sites are allowed, who may use the
  browser, share windows or show remote desktops (everyone / trusted players / only the host), how much control a
  viewer may be given, how many screens may be in use, stream limits, and whether players may connect directly. The
  host can stop any screen's sharing.
- **The player sharing** (the browser, window or remote desktop runs on their PC) chooses what to share, can blank it
  for everyone at any time (**F10**, which also takes all control back), and decides who may control it, within the
  host's rules. Other players only get the picture and sound: never their other windows, their VNC addresses or
  passwords.

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
  A notice explains this the first time.
- **Remote desktops:** Telescreen encrypts the connection when the VNC server offers VeNCrypt with an X.509
  certificate (QEMU `tls-creds-x509`, Proxmox, TigerVNC `X509Vnc`), and the panel shows whether a connection is
  encrypted. Plain VNC isn't: use it on your own network or the same PC. "Only connect to encrypted remote desktops"
  (Mods > Telescreen > When you share) refuses plain servers. VNC's own password login only uses the first 8
  characters of a password.
- What players show on screens comes from them and from third-party sites; Telescreen doesn't check it. Hosts can
  limit who may share.
- Downloading or streaming from YouTube may be against its terms of service; use it responsibly.

## Known limitations

- Without direct connections, streams go through the world host's connection; a slow host upload lowers quality (the
  stream adapts), and lost packets freeze the picture until the next full frame. Lower the stream bitrate, or turn on
  direct connections, if that happens often. Direct connections don't adapt the bitrate yet: the sharer's upload has
  to carry each direct viewer's copy.
- The first video on a PC takes 5-10 seconds to start (link lookup). If YouTube links stop loading, the bundled yt-dlp
  may need an update (a mod update).
- Clicking the 3D screen directly isn't supported: use its panel and window.
- Not yet tested with more than one player (see above).
- Subtitles: picture-based tracks (Blu-ray PGS, DVD) aren't supported, only text; a player watching a video streamed
  from another player gets no subtitles.
- Remote desktops: anonymous TLS (TigerVNC's default `TLSVnc`) isn't supported, only TLS with a certificate.
- The browser's process sandbox is off. RDP isn't supported yet (VNC is).

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

## Network use and privacy

Telescreen connects only to the video sites, web pages and VNC servers players choose, plus the browser's search
engine; see the network activity section for the full list. Everything between players goes through the game's own
connection, unless you turn on direct connections (off by default). No telemetry or analytics.

# NETWORK ACTIVITY TRANSPARENCY

Telescreen connects to these services. Everything between players (the video queue and position, live streams of
browsers, windows, desktops and videos, viewer input and pointers) goes through the game's own connection to the
world host, except live streams when direct connections are on (below; off by default).

- **Video sites** (YouTube, Twitch, Vimeo and others, through the bundled yt-dlp and FFmpeg): when you add a link, your
  PC looks it up (title, length); when a video plays on a screen near you, your PC fetches it from the site and its
  video servers. This includes videos other players added: by default your PC loads those only from the sites on
  your allowed list (Mods > Telescreen > Privacy; you can also choose any site, or never). The world host's list
  decides which sites can be added at all (unless the host allows any site). Subtitles you pick are downloaded from
  the same site. QuickJS, which yt-dlp uses for YouTube, runs locally and connects nowhere.
- **Web pages in the built-in browser** (Chromium Embedded Framework): only on the PC of the player using the
  browser, to the pages they open, as any browser does. Words typed in its address box go to the search engine in the
  settings (default DuckDuckGo, duckduckgo.com); its start page is also DuckDuckGo by default. Players watching the
  screen receive the picture and sound through the game and don't connect to those pages. Logins and cookies are
  forgotten when the browser closes unless "Keep browser logins" is on (then kept in
  `%LOCALAPPDATA%\FactoryGame\Saved\Telescreen\Browser`). Extensions are disabled.
- **VNC servers** (Remote desktop tab): only the address you enter, from your PC, while your screen shows it.
  Encrypted with TLS when the server offers VeNCrypt with a certificate; plain VNC isn't encrypted, so use that on
  your own network. Saved connections are stored locally in
  `%LOCALAPPDATA%\FactoryGame\Saved\Telescreen\RemoteConnections.json`, passwords encrypted for your Windows user;
  the certificates seen are remembered in `VncCertificates.json` next to it. Only a random token, never the address
  or password, is sent to the other players.
- **Shared windows** are captured on your PC and sent only through the game's connection; no other service is
  contacted.
- **Your video files** are read from your PC only. When another player can't play a video that plays on your PC, your
  PC may stream it to them through the game's connection (setting "Stream videos to players who can't play them").
  Your videos folder is indexed locally (file names only) and never sent anywhere; only the queued file's path or
  name goes to the other players, as part of the queue.
- **Direct connections** (only when you turn on "Direct connections for streams"; off by default): your PC asks a
  public STUN server (stun.l.google.com, or stun.cloudflare.com) for its public IP address and port, and with "Open the
  port on the router" on, asks your home router over UPnP to open one UDP port while the game runs (removed when the
  game closes, and expiring after two hours in any case). When you watch or share a stream with another player who
  also has it on, your PCs exchange their addresses (public and local) through the world host and then send the
  stream straight to each other over UDP,
  encrypted with AES-GCM under a key only the two of you get. That player learns your public IP address. The world
  host can turn direct connections off for the world.

The helper program talks to the game through a local pipe and shared memory on your PC. The mod downloads no updates
or other files, and has no telemetry or analytics.

# AI USAGE TRANSPARENCY

This mod was built with the help of an AI coding assistant (Anthropic's Claude):

- **Source code:** most of the mod's C++ source code (the mod and its helper program) was written by the AI,
  directed, reviewed and tested in game by the author.
- **Mod page text:** this description was written by the AI and reviewed by the author.
- **Developer documentation** in the source repository was written by the AI.
- **Art:** the mod icon is a flat drawing generated by a small script the AI wrote (no image-generation AI). The
  screenshots are from the author's game. No other AI-generated assets: the UI uses the game's own widgets, fonts and
  icons, and the screens use the game's billboard models.
- **In game:** the mod doesn't use or give access to generative AI.
