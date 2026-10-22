# Publishing Telescreen on ficsit.app (SMR)

First upload: Telescreen isn't on SMR yet (checked 2026-10-22), so the mod has to be created before its first
version.

## 1. Create the mod (ficsit.app, signed in, "Create mod")

| Field | Value |
|---|---|
| Name | Telescreen |
| Mod reference | `Telescreen` (must match `Telescreen.uplugin`; can't be changed later) |
| Short description | Billboard screens for synced videos, a shared browser, shared windows and VM desktops that every player sees and hears. |
| Full description | everything below the `---` line in `Docs/Release/ModPage.md` |
| Logo | `Docs/Images/icon-1080.png` (1080x1080) |
| Source URL | `https://github.com/benvrakas/Telescreen` only once the repository is public (it is private now); otherwise leave empty |
| Tags | `multiplayer`, `decoration`, `api` (it fetches from video sites) |
| Compatibility | Stable: works (built against CL 513885). Experimental: unknown until tested |

## 2. Upload the version

| Field | Value |
|---|---|
| File | `D:\SFMod\SatisfactoryModLoader\Saved\ArchivedPlugins\Telescreen\Telescreen.zip` (the combined zip: Windows client, Windows and Linux servers) |
| Version | read from the zip's `Telescreen.uplugin`: SemVersion `0.1.0` (VersionName `0.1.0-beta.11`) |
| Stability | **Beta** |
| Changelog | the text below |

Changelog for the first upload:

```
First public beta.

- Small and Large Telescreen (AWESOME Shop, Signs, Telescreen Set).
- Synced videos (YouTube, Twitch, Vimeo, files) with a shared queue, seek bar and 3D sound through the game's audio.
- Shared built-in browser, shared windows and remote desktops (VNC), streamed to every player through the game's connection.
- Viewer control and pointers, privacy curtain (F10), picture-in-picture on the HUD, screens list (F9).
- Stream resolution, bitrate, frame rate and encoder settings; world host rules and limits.
```

Later versions: the next SemVersion after the newest on SMR (`0.1.1`, `0.2.0`...), with `VersionName` matching it.

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
- **Test first, in multiplayer too.** Single player was tried on beta 10 (fixes in beta 11). Nothing has been tested
  with a second player yet: streaming to viewers, viewer control, the world host's limits and the screen volume all
  depend on it. Upload after a multiplayer session with beta 11 or later (and repackage the full zip then).
