# Telescreen 1.0: multiplayer test guide

Thanks for testing! Telescreen 1.0 has only been tried in single player so far; this guide is for the first sessions
with other players. Please read "What works" first, so you know what to try and what is deliberately missing.

## Install (everyone in the session needs the same version)

Requirements: Satisfactory (Steam or Epic, the same game version as the host) and **SML** (Satisfactory Mod Manager
installs it). Windows only: the helper program is Windows only. A dedicated server can run the mod too (server zips
exist) but isn't needed for this test.

1. In Satisfactory Mod Manager, install **Telescreen** from the mod list (once it's published), or download
   `Telescreen-Windows.zip` and use **Import local mod**. Or by hand: extract the zip's contents into `<Satisfactory>\FactoryGame\Mods\Telescreen\` (create the folder; the
   file `Telescreen.uplugin` must sit directly inside it). Remove an older Telescreen first.
2. The download is about 270 MB because it carries its own helper programs (video decoder, YouTube downloader, browser).
   **Windows Defender or another antivirus may warn about `TelescreenHost.exe`** (it is a new unsigned program that
   opens a browser, captures a window you choose and downloads video). It is built from this repository's source; if
   your antivirus quarantines it, allow it for the Telescreen folder.
3. Start the game with the mod. The first start takes a few seconds longer while the helper files are checked.

## What works

- **Screens.** AWESOME Shop, Signs: **Telescreen Set** (25 FICSIT Coupons) unlocks the **Small Telescreen** (8 m x
  4 m) and **Large Telescreen** (16 m x 8 m), built like billboards. They can be rescaled with scaling mods.
- **The screen panel** (use a screen): the screen's live picture, what it shows, **My volume**, the screen's volume
  for everyone, **Show on HUD** and **Screen settings** (name, picture fit) on the left; the mode tabs pick the page
  on the right.
- **Videos tab.** Paste a YouTube, Twitch or Vimeo link, **Play now** or **Add to queue**. Everyone in the session
  sees and hears the same video at the same moment, with 3D sound from the screen. Seek bar, pause, next, stop, a
  shared queue showing who added what (click an entry to play it). Playing a video on a screen that shows a browser,
  window or desktop switches it back to videos.
- **Your video files.** **Pick a file** (or paste a path). Each player sets their **videos folder** (Change folder):
  a file someone else added plays from your copy, found by name in that folder. If you don't have it, you watch it
  streamed from a player who does.
- **Subtitles.** Off / On screen / On HUD / Both in the Videos tab, then a track: the site's subtitles or automatic
  captions, tracks inside a file, or a .srt next to it.
- **Browser tab.** **Open the browser** (a first-time notice asks you to confirm that others see everything in it).
  Its window opens by itself: click and type in it; F8 or holding Esc returns to the game, F8 opens it again. Words in
  the address box are searched (search engine in the settings). **Everyone near the screen sees and hears it**,
  streamed from your PC through the game's connection. Web games that lock the mouse work.
- **Share a window tab.** Your open windows are listed; click one to show it on the screen with its own sound. It may
  stay behind the game but not be minimised. By default only the world host and trusted players may share windows.
- **Remote desktop tab (VNC).** Address and password, **Connect** (or **Save** it for later). Mouse and keyboard go
  to the computer in its window (F8); QEMU VMs send their sound. By default only the world host may use it.
- **Viewer control.** While you share, the panel lists who watches; give one of them the mouse, or mouse and typing,
  for 15 minutes (within the world host's limits). F10 takes it back. Viewers can click the picture to point at
  something for everyone.
- **Privacy.** While you share, a red line at the top of your HUD says what you share and how many watch. **F10**
  blanks it for everyone (you keep seeing it), F10 again shows it.
- **Show on HUD** and **F9** (all screens).
- **Direct connections** (Mods menu, Telescreen, Direct connections; off by default). When both the sharer and a
  viewer turn it on, the stream goes straight between their PCs instead of through the world host. The viewer's
  stream line in the panel then starts with "direct".
- **Settings** (Mods menu, Telescreen). *When you share*: stream resolution, bitrate, frame rate, encoder, browser
  page size, start page, search engine. *Videos on this PC*, *Subtitles*, *Direct connections*. *Rules for this world
  (world host)*: who may use each mode, trusted players, viewer control limits, stream limits (bitrate, resolution,
  frame rate), allowed video sites, direct connections. The world host gets a "Stop it (world host)" button on
  screens others use.

## What does not exist yet

RDP (VNC works), an in-game panel for the world host's rules, clicking the 3D screen directly, subtitles for picture
tracks (Blu-ray PGS) and for videos streamed from another player. Don't look for them.

## Please test (multiplayer)

Host a world with one friend joining (listen server). Everybody does the same checks; note anything odd.

1. Both can see the Telescreen Set in the AWESOME Shop; the host builds a Small and a Large Telescreen.
2. The host adds a video. The friend (near the screen) should see and hear it within about a second of the host.
   Try different distances: sound should get quieter and come from the screen.
3. The friend joins *while* a video is playing: it should start at the right moment, not from 0.
4. The friend pauses, seeks, skips, adds videos. The host should see all of it, and the queue shows who added what.
5. **Browser stream:** the host opens the browser on a screen and plays a YouTube video and a web game in it. The
   friend should see it move smoothly and hear it from the screen. Then swap: the friend opens it, the host watches.
   Note how sharp and how far behind the picture is, and any freezes.
6. **Curtain:** the one sharing presses F10: the other sees "They paused the stream"; F10 again brings it back.
7. **Window:** the host shares a window (e.g. a video player or a music app). The friend sees and hears it. The
   friend tries too: by default they get "Only trusted players can share a window"; the host adds the friend's name
   to Trusted players (Mods menu, Telescreen, Rules for this world) and the friend tries again.
8. Walk 150+ m away: the screen stops playing on your PC and resumes when you come back (for streams, the sharer's
   HUD line drops to 0 watching).
9. Show on HUD (panel button): picture and sound follow you, also for a shared browser.
10. Save and reload the world: screens come back with their queue and position (shared browsers and windows end).
11. Leave and rejoin; close the game while sharing (the helper should stop with it; check Task Manager for a leftover
    `TelescreenHost.exe` afterwards, there should be none).
12. While sharing, does movement or building lag for the one sharing or the one watching? Tell us your upload speed.
13. Does the screen volume (everyone) change it for the other player too? Change the stream resolution (e.g. 480 and
    1080) while sharing: the viewer's picture should get softer / sharper after a second. The world host lowers
    "Highest stream resolution" to 480: the sharer's stream follows.
14. **Video files:** the host plays a file only the host has: the friend sees "streamed from" and watches it live.
    Then both have the same file in different folders: each sets their videos folder; each plays their own copy.
15. **Subtitles:** a YouTube video with captions, Subtitles On screen, then On HUD; a local file with a .srt next to it.
16. **Direct connections:** both turn them on (Mods menu, Telescreen, Direct connections); one shares the browser.
    The viewer's stream line should start with "direct" after a few seconds. Turn it off on one side: the stream
    should continue through the host. Tell us whether it connected, and whether either of you has UPnP on the router.
17. **Remote desktop control:** the host (who shows a remote desktop) gives the friend the mouse: the dialog asks every
    time, and the panel's Control log lists the grant, then "control ran out" or the F10 take-back. With a VNC server
    that has a certificate (QEMU tls-creds-x509, Proxmox), the Remote desktop tab says "Encrypted (TLS ...)".

## If something breaks: send these

- `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log` (search for `LogTelescreen`)
- `%LOCALAPPDATA%\FactoryGame\Saved\Logs\TelescreenHost.log` and `TelescreenBrowser.log`
- What you pressed, what you expected, what happened, and whether you were host or client.

## Known issues in this build

- Picture may be mirrored or upside down on a screen (not yet seen in the game). If so: Mods menu, Telescreen,
  Diagnostics, "Mirror picture left-right" / "Flip picture upside down". Please tell us which one fixed it.
- The first video on a PC takes 5-10 seconds to start (YouTube lookup).
- Without direct connections, streams go through the host's upload; a slow upload lowers quality (the stream
  adapts) and each lost packet freezes the picture until the next full frame. Lower "Stream bitrate (kbps)" (Mods
  menu, Telescreen, When you share) if it freezes often, or try direct connections.
- A shared window's sound needs Windows 11 or a recent Windows 10; you hear the window yourself as usual, others hear
  it from the screen.
- Only one picture-in-picture at a time; the window can't be dragged or resized yet.
- Clicking the 3D screen directly isn't supported; use the panel and the window.
- The browser's process sandbox is off.
- YouTube changes things often; if links stop loading, the bundled yt-dlp may need an update.
