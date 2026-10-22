# Changelog

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
