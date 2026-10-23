# Changelog

## 1.0.0

First public release (everything from the betas below). Not yet tested in multiplayer.

- New mod icon; the mod page has screenshots.
- **Videos folder**: each player sets theirs (Videos tab, Change folder; or Mods > Telescreen > Videos on this PC).
  A file someone else added plays from your copy: the same path, or a file with the same name anywhere in your videos
  folder. **Pick a file** chooses a video with the Windows file picker.
- **A video you can't play is streamed to you** by a player who plays it (you don't have the file, or the link fails
  on your PC), like a shared window. Setting "Stream videos to players who can't play them" (on).
- **Subtitles**: on the screen, on your HUD, both or off (Videos tab, or Mods > Telescreen > Subtitles). From the
  video's site (with YouTube's automatic captions), text tracks inside files, or a .srt / .vtt next to the file;
  your preferred language is picked first.
- **Direct connections** (Mods > Telescreen > Direct connections, off by default): live streams go straight between
  the sharer's and each viewer's PC, encrypted (AES-GCM), instead of through the world host. STUN for the public
  address, UPnP or a forwarded port when needed, the world host's connection as the fallback. The world host can
  turn them off ("Allow direct connections between players").
- **Encrypted remote desktops**: VeNCrypt with TLS (Windows' own TLS) when the VNC server has an X.509 certificate;
  the certificate is remembered on the first connection and a changed one is refused until you trust it ("Trust the
  new certificate"). The panel shows whether the connection is encrypted; "Only connect to encrypted remote desktops"
  refuses plain VNC. Optional user name for servers that ask for one.
- **Control log** in the sharing card (for the sharer and the world host): who got control, for how long, when it
  ran out or was taken back. Giving a viewer control of a remote computer is confirmed every time.
- A warning that VNC's password login only uses the first 8 characters.
- **Picture subtitles** (Blu-ray PGS, DVD) inside video files, drawn into the picture on the screen.
- **`/ts list`** and **`/ts stop <number|all>`** chat commands (also `/telescreen`), like BoomBoxPlus's `/bbp`.
- Sharper text on screens (status and subtitles use the UI font at full size, the status picture is twice as large).
- With "Mute when the game is in the background" off, screens stay audible in the background even when the game's own
  "Mute When Minimized" is on (it silences the game's audio engine; each screen keeps the master volume instead).
- Fix: after a lag spike (an autosave) a video's sound could play noticeably off-pitch until pause and play: keeping it
  in sync changed the audio's speed by up to 5%. Now at most 0.4% (inaudible); bigger drift (an autosave stalls the game's audio for about 0.3 s) skips to the right spot.
- Fix: video files had no seek bar (and didn't move on to the next one by themselves): their length now comes from the
  first player who plays them.
- Fix: long links and file paths were cut short by the game's text field (about 80 characters), so long local file
  paths couldn't be found. Text fields now take up to 2048 characters.

## 0.1.0 beta 11

- Fix: a screen could stay black while its panel's preview showed the picture (most often a VM, whose desktop changes
  size while it boots). Every screen's picture now has its own texture.
- Fix: Letterbox / Fill / Stretch apply at once, also to a still picture (a paused video or an idle desktop). The
  chosen one is highlighted.
- The window title is the screen's name again (it said "Videos"). The tabs are the four modes; **Screen settings**
  (name, picture fit) is a button under the volume sliders.
- **Screen volume for everyone** next to My volume (who may change it follows "Who can change screen videos").
- The VNC password field is a game-style text field; the panel is a little wider so nothing is cut off.
- Remote desktops are asked to switch to the screen's shape (ExtendedDesktopSize). TigerVNC and x11vnc follow;
  QEMU's standard VGA display refuses, so use **Fill** for those. Setting: When you share, "Ask remote desktops for the
  screen's shape".

## 0.1.0 beta 10

- **Bigger screen panel** in two columns: the screen's live picture, what it shows, My volume and the sharing card
  on the left; the page on the right. Tabs are the game window's own header tabs, with icons. The queue, the
  shareable windows and saved remote desktops are game-style list entries (click one to play, share or connect).
- **Stream settings** (Mods menu, Telescreen, When you share): stream resolution (default 720 lines, taller pictures
  are scaled down), bitrate, frame rate, and encoder (best, AV1, HEVC, H.264 or software AV1); browser page size is
  its own setting now. Changes apply while you share.
- **World host limits** (Rules for this world): highest stream bitrate, resolution and frame rate. Sharers' PCs keep
  within them.
- Streams above about 6 Mbps are no longer held back by a fixed send rate.

## 0.1.0 beta 9

- Playing a video on a screen that shows a browser, window or remote desktop switches it back to videos (for the
  player sharing and the world host always; for others when taking over is allowed). Adding to the queue only adds,
  and says the screen is busy.
- A seek bar replaces -10 s / +10 s: drag anywhere in the video.
- **My volume** in the screen panel: your own volume for each screen, 0-200%, remembered on your PC.
- Browser search engine setting (When you share, default DuckDuckGo): words typed in the address box are searched;
  `youtube.com`-style names open as sites.

## 0.1.0 beta 8

- Fix: remote desktops (VNC) disconnected after a few seconds of desktop drawing (large pictures were cut short while
  decoding). Tested through a whole VM boot against QEMU's VNC server.

## 0.1.0 beta 7

- **Sound through the game's audio engine** (Wwise), as BoomBoxPlus plays its music: rooms, reverb, walls and the
  game's volume sliders apply. Works alongside BoomBoxPlus. Mods menu, Telescreen, Audio switches it off.
- **Remote desktop (VNC):** a screen panel tab connects to a VM console (QEMU/KVM, Proxmox) or any VNC server, with
  mouse and keyboard in its window, sound from QEMU, and saved connections (passwords encrypted for your Windows
  user). Only the world host may use it by default.
- **Giving viewers control:** the player sharing can let a watching player use the mouse, or mouse and typing (or
  everything, in the browser), for 15 minutes, within the world host's limit. F10 (blank) also takes control back.
  Viewers' cursors and names show on the screen; any viewer can click to point at something for everyone.
- Settings split into **When you share (your PC)** and **Rules for this world (world host)**; texts say "world host"
  for the world's host and admins.

## 0.1.0 beta 6

- Tabs are game-style buttons with their names (orange when selected); they were empty boxes.
- Text fields no longer show a search icon.
- The sharing line at the top of the HUD and the screens list (F9) are centred; they could sit in the corner, cut off.

## 0.1.0 beta 5

- The screen panel and the screens list (F9) use the game's own window frame, buttons, tabs, text fields and scroll
  boxes, and the game's font.
- Enter in a text field runs its action (play the link, open or go to the address, save the name).

## 0.1.0 beta 4

- The screen panel has tabs: Videos, Browser, Share a window, Screen. The header says what the screen shows and
  holds the sharing controls (use it in a window, blank for everyone, stop) while you share.
- First-time sharing notices are a dialog with "I understand, continue" and Cancel (instead of pressing twice).
- Opening the browser opens its window by itself.
- The Share a window tab lists your windows as soon as it opens; each has a "Share this window" button.
- Fix: the browser window opened in the top-left corner, cut off; it is now centred (and the game behind it dimmed).

## 0.1.0 beta 3

- Fix: built screens showed only the post (no billboard, no screen, nothing to use). The vanilla billboard assets
  set on the class at start-up never reached placed screens; each screen now copies them when it is created.

## 0.1.0 beta 2

- **Live streaming:** a player's built-in browser is streamed to everyone near the screen through the game's own
  connection (HEVC/AV1/H.264 on the GPU, Opus sound), adapting to the upload speed.
- **Share a window:** show one window of your PC on a screen, with its own sound.
- Privacy: a sharing notice the first time, a HUD line while you share (with viewer count), F10 to blank the stream.
- Host settings: who can use the browser and share windows (everyone / trusted players / host), trusted players,
  taking over screens in use, screens in use at once, stream rate cap. "Only the host" means admins on dedicated
  servers. Host/admin "Stop it" button.
- Fixes: long links or names no longer disconnect a player; the allowed-sites check can't be fooled by `?` or `@` in
  a link; a full queue no longer drops the playing video; F8 only grabs the mouse for something you can control.

## 0.1.0 beta

First test build.

- Small and Large Telescreen, unlocked in the AWESOME Shop (Telescreen Set, 25 coupons).
- Synced videos from YouTube, Twitch, Vimeo and local files, with a shared queue, 3D sound, pause/seek, picture fit.
- Show on HUD (picture-in-picture), screens list (F9).
- Built-in browser on a screen (only the player using it sees it), interactive window (F8), mouse lock for web games.
- Settings for performance, audio, privacy, hosting rules, HUD picture and the browser.

**Good to know**

- Everyone in a multiplayer session needs the same Telescreen version.
- Remote desktops (RDP/VNC) are planned, not in this build.
