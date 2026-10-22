# Telescreen

Billboard-sized screens for Satisfactory. Put a screen up, paste a video link, and everyone near it watches the same
video at the same moment, with sound that comes from the screen.

## Features

- **Small and Large Telescreen**, built from the billboard models, unlocked in the AWESOME Shop. Rescale them with a
  scaling mod and the picture follows.
- **Synced videos**: YouTube, Twitch, Vimeo and video files. A shared queue (everyone sees who added what), pause,
  seek, next, 3D sound, letterbox / fill / stretch.
- **Picture-in-picture on your HUD** for any screen, with sound that follows you.
- **Built-in browser** on a screen, with a window you can use with mouse and keyboard, including web games that lock
  the mouse. (Beta: only the player using it sees it. Sharing it with everyone is the next step.)
- Settings for host and player: allowed sites, who can change videos, how many screens play at once, volume, privacy.

## Requirements

SML. Windows (the video and browser helper is Windows-only). Everyone in a multiplayer session needs the mod.

## Good to know

- The mod ships its own helper programs: a video player (FFmpeg), a link reader (yt-dlp with QuickJS) and a browser
  (Chromium). They run only on your PC, never on dedicated servers. Antivirus programs may flag the unsigned
  `TelescreenHost.exe`.
- Videos are fetched by each player's own PC, from the video site. Videos added by other players load automatically
  only from sites on your allowed list.
- Downloading or streaming from YouTube may be against its terms of service; use it responsibly.

## AI disclosure

Written with the help of Claude (Anthropic).
