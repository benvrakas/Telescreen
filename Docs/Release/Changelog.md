# Changelog

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
