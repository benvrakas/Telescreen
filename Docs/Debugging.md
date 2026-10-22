# Debugging Telescreen

## Logs

- **Game:** `LogTelescreen` in `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log` (dedicated servers: the
  server's `FactoryGame/Saved/Logs`).
- **Helper:** `%LOCALAPPDATA%\FactoryGame\Saved\Logs\TelescreenHost.log`, rewritten each time the helper starts.
  FFmpeg's own warnings and errors appear there as `FFWRN` / `FFERR`.

What to look for:
- At game start: `Resolving vanilla assets for Telescreen content` and one line per screen size listing assets as
  `ok` or `MISSING`; `Runtime components verified ... host ok, fetcher ok` (otherwise a line says which file is
  missing or doesn't match its checksum).
- `<screen> began play at ..., screen W x H cm, N queued, playing I` for every screen.
- `Host: started TelescreenHost (pid ...)`, `Host: connected`, `Host: ready` when the first video plays.
- Per video: `Host: session N opens <url>`, the helper's `yt-dlp resolve: exit 0 in X s`, `video stream ... uses
  libdav1d`, `playing from T`, and in the game log `video texture WxH`, `audio 48000 Hz x2, heard up to N m`.
- Server side: `Server_Enqueue from <player>`, `command ... -> item I, playing at T s`, `item I finished`.

Verbose detail needs `-LogCmds="LogTelescreen Verbose"` on the game's command line, or `Log LogTelescreen Verbose`
in the console.

## Testing without the game

- `Helper\build.bat` builds the helper; `python Tools\stage_runtime.py` stages it with FFmpeg, yt-dlp and QuickJS
  and regenerates the manifest (rebuild the mod afterwards).
- `Helper\out\TelescreenHost.exe --selftest <url> --ytdlp <yt-dlp.exe> --jsruntime <qjs.exe> --seconds 20 --start 60
  --seek-at 8 --seek-to 200 --pause-at 12 --resume-at 15 --out frame.bmp` plays a video while acting as the mod's
  side of the shared buffers, prints target vs. position once a second and saves the last frame. `--selftest-file`
  plays a local file.
- `python Tools\host_pipe_test.py <url> [seconds]` drives the staged helper over its pipe like the mod does.
- `TelescreenHost.exe --selftest-browser <url> --browserdir <Runtimerowser> --browserdata <dir> --seconds 10` runs a
  browser session with scripted input against `Tools/browser_test.html` (page console lines appear as `page:`).
- `TelescreenHost.exe --selftest-encode <video> --width 1440 --height 720 --fps 60 --kbps 3000 --frames 600
  [--prefer libsvtav1 --software 1] [--out frame.bmp]` benchmarks the stream encoder and decoder.

## Common problems

- Screen shows "Telescreen component 'host' is not installed correctly": the runtime folder is missing or a file
  doesn't match the manifest; restage and rebuild, or reinstall the mod.
- YouTube very slow or failing: yt-dlp needs QuickJS for YouTube's challenges (without it YouTube throttles to
  about 200 KB/s); check that the helper log's yt-dlp line has no error.
- Picture mirrored or upside down: Mods menu, Telescreen, Diagnostics.
- Not in the AWESOME Shop: look for the schematic line (`cost entries 1, shop subcategories 1, unlocks 1`).
