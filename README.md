# Telescreen

A Satisfactory mod that adds Small and Large Telescreens: billboard-sized screens that play synced videos, a shared
built-in browser, windows and remote desktops for every player, with 3D audio.

Work in progress. Current state (M1): the screens are bought in the AWESOME Shop (Telescreen Set, 25 coupons) and
built like billboards. Using a screen opens its panel: paste a YouTube, Twitch or Vimeo link (or a video file path)
and every player sees and hears the same video, in sync, from the screen's position.

Layout:
- `Source/Telescreen`: the mod (C++ only).
- `Helper`: `TelescreenHost.exe`, the helper process that fetches and decodes media (`Helper\build.bat`).
- `Tools`: runtime staging, server zip trimming, test drivers, asset dump.
- `Docs`: `Architecture.md`, `HostProtocol.md`, `Debugging.md`, `ReverseEngineering.md`.

Third-party components shipped in `Resources/Runtime` (staged, not committed): FFmpeg (LGPL), yt-dlp (Unlicense),
QuickJS-ng (MIT); licenses are copied next to them.
