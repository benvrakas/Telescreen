# Publishing Telescreen on ficsit.app (SMR)

First upload: Telescreen isn't on SMR yet (checked 2026-10-22), so the mod has to be created before its first
version.

## 1. Create the mod (ficsit.app, signed in, "Create mod")

| Field | Value |
|---|---|
| Name | Telescreen |
| Mod reference | `Telescreen` (must match `Telescreen.uplugin`; can't be changed later) |
| Short description | Billboard screens for synced videos, a shared browser, shared windows and VM desktops that every player sees and hears. |
| Full description | the FULL DESCRIPTION section of `Docs/Release/ModPage.md` |
| Network activity transparency | the NETWORK ACTIVITY TRANSPARENCY section |
| AI usage transparency | the AI USAGE TRANSPARENCY section (edit the `[bracketed]` line first) |
| Logo | `Docs/Images/icon-1080.png` (1080x1080) |
| Source URL | `https://github.com/benvrakas/Telescreen` (public since 2026-10-22) |
| Tags | `multiplayer`, `decoration`, `api` (it fetches from video sites) |
| Compatibility | Stable: works (built against CL 513885). Experimental: unknown until tested |

## 2. Upload the version

| Field | Value |
|---|---|
| File | `D:\SFMod\SatisfactoryModLoader\Saved\ArchivedPlugins\Telescreen\Telescreen.zip` (the combined zip: Windows client, Windows and Linux servers) |
| Version | read from the zip's `Telescreen.uplugin`: `1.0.0` |
| Stability | **Release** |
| Changelog | the text below |

Changelog for the first upload:

```
First release. Not yet tested in multiplayer: please report how it goes with friends.

- Small and Large Telescreen (AWESOME Shop, Signs, Telescreen Set).
- Synced videos (YouTube, Twitch, Vimeo, files) with a shared queue, seek bar and 3D sound through the game's audio.
- Shared built-in browser, shared windows and remote desktops (VNC), streamed to every player through the game's connection, or over optional direct connections (encrypted, UPnP/STUN).
- Your own video files: every player plays their copy (found by name in their videos folder); players without it watch it streamed.
- Subtitles on the screen, on the HUD, both or off (site subtitles, YouTube captions, tracks in files, .srt/.vtt).
- Viewer control and pointers with a control log, privacy curtain (F10), picture-in-picture on the HUD, screens list (F9).
- Encrypted remote desktops (VeNCrypt TLS, certificate remembered on first use).
- Per-player and per-screen volume; stream resolution, bitrate, frame rate and encoder settings; world host rules and limits.
```

Later versions: the next version after the newest on SMR (`1.0.1` for fixes, `1.1.0` for features).

## 3. Before uploading: open points for you

- **Executables.** The zip ships `TelescreenHost.exe`, FFmpeg, yt-dlp, QuickJS and CEF (Chromium). ficsit.app staff
  (SirDigby, 2026-10-21) said they weren't prepared for mods that package executables and a policy may follow.
  Expect the upload to be held for review or questioned; asking staff first is safest. A `slim` package (no
  executables, prompted download) exists in the build script, but the in-game download prompt isn't built yet, so a
  slim upload wouldn't work on its own.
- **Size.** The combined zip is large (the Windows client part alone is about 270 MB, the browser runtime most of it).
  If SMR refuses the file size, tell me: the browser could become a separate download.
- **Licences.** Each runtime folder carries its licence and a `SOURCE.txt` (FFmpeg LGPL v3 with its source link,
  CEF/Chromium credits, yt-dlp, QuickJS, miniz). The mod page's "Credits and licences" lists them.
- **Multiplayer is untested** (your decision for 1.0, 2026-10-22); the mod page says so at the top.
- **Screenshots** load from the public repository (`Docs/Images/*.jpg`, checked 2026-10-22).
