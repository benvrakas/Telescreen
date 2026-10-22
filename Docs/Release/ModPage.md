# Telescreen

Billboard-sized screens for Satisfactory. Put a screen up, paste a video link, and everyone near it watches the same
video at the same moment, with sound that comes from the screen. Or open a browser on it, or share a window from your
PC, and everyone watches that.

## Features

- **Small and Large Telescreen**, built from the billboard models, unlocked in the AWESOME Shop. Rescale them with a
  scaling mod and the picture follows.
- **Synced videos**: YouTube, Twitch, Vimeo and video files. A shared queue (everyone sees who added what), pause,
  seek, next, 3D sound, letterbox / fill / stretch.
- **Shared built-in browser**: use it in a window with mouse and keyboard (web games that lock the mouse work);
  everyone near the screen sees and hears it, streamed through the game's connection. No ports to open.
- **Share a window** from your PC (a video player, a game, a VM window) with its sound.
- **Privacy**: a notice before you share, a HUD line while you do, a key that blanks the stream for everyone.
- **Picture-in-picture on your HUD** for any screen, with sound that follows you.
- Settings for host and player: who can use the browser or share windows, allowed sites, who can change videos, how
  many screens play at once, stream quality, volume, privacy.

## Requirements

SML. Windows (the video and browser helper is Windows-only). Everyone in a multiplayer session needs the mod.

## Good to know

- The mod ships its own helper programs: a video player (FFmpeg), a link reader (yt-dlp with QuickJS) and a browser
  (Chromium). They run only on your PC, never on dedicated servers. Antivirus programs may flag the unsigned
  `TelescreenHost.exe`.
- Videos are fetched by each player's own PC, from the video site. Videos added by other players load automatically
  only from sites on your allowed list.
- What players show on screens comes from them and from third-party sites; Telescreen doesn't check it. Hosts can
  limit who may share.
- Downloading or streaming from YouTube may be against its terms of service; use it responsibly.

## AI disclosure

Written with the help of Claude (Anthropic).
