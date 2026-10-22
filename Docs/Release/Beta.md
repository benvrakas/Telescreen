# Telescreen 0.1.0 beta 3: tester guide

Thanks for testing! This is an early build. Please read "What works" first, so you know what to try and what is
deliberately missing.

## Install (everyone in the session needs the same version)

Requirements: Satisfactory (Steam or Epic, the same game version as the host) and **SML** (Satisfactory Mod Manager
installs it). Windows only: the helper program is Windows only. A dedicated server can run the mod too (server zips
exist) but isn't needed for this test.

1. Download `Telescreen-Windows.zip` from the same place you got this guide.
2. In Satisfactory Mod Manager: pick your profile, **Import local mod**, choose the zip.
   Or by hand: extract the zip's contents into `<Satisfactory>\FactoryGame\Mods\Telescreen\` (create the folder; the
   file `Telescreen.uplugin` must sit directly inside it). Remove an older Telescreen first.
3. The zip is about 270 MB because it carries its own helper programs (video decoder, YouTube downloader, browser).
   **Windows Defender or another antivirus may warn about `TelescreenHost.exe`** (it is a new unsigned program that
   opens a browser, captures a window you choose and downloads video). It is built from this repository's source; if
   your antivirus quarantines it, allow it for the Telescreen folder.
4. Start the game with the mod. The first start takes a few seconds longer while the helper files are checked.

## What works

- **Screens.** AWESOME Shop, Signs: **Telescreen Set** (25 FICSIT Coupons) unlocks the **Small Telescreen** (8 m x
  4 m) and **Large Telescreen** (16 m x 8 m), built like billboards. They can be rescaled with scaling mods.
- **Synced videos.** Use a screen: paste a YouTube, Twitch or Vimeo link, **Play now** or **Add to queue**. Everyone
  in the session sees and hears the same video at the same moment, with 3D sound from the screen. Pause, seek (-10 s /
  +10 s), next, stop, a shared queue showing who added what, picture fit (letterbox / fill / stretch), screen names.
- **Shared browser.** "Open the browser on this screen" starts a built-in browser. **Everyone near the screen sees
  and hears it**, streamed from your PC through the game's connection (no ports to open). F8 opens it in a window you
  can click and type in; hold Esc or press F8 to go back to the game. Web games that lock the mouse work. The first
  time, a notice reminds you that others see everything in it.
- **Shared window.** "Share a window", "Choose a window", then Share: one window of your PC (a video player, a game,
  a VM window...) shows on the screen with its own sound. It may stay behind the game but not be minimised. In the
  screen window (F8) clicks and keys are sent to it; most normal apps accept them, games and VMs usually don't. By
  default only the host and trusted players may share windows (see host settings).
- **Privacy.** While you share, a red line at the top of your HUD says what you share and how many watch. **F10**
  blanks it for everyone (you keep seeing it), F10 again shows it.
- **Show on HUD.** A small picture-in-picture of a screen in a corner of your HUD, with its sound everywhere. F9 lists
  all screens.
- **Host settings** (Mods menu, Telescreen, When hosting): who can use the browser and share windows (everyone /
  trusted players / only the host), a trusted players list, whether players can take over a screen in use, how many
  screens may be in use at once, the stream rate cap, allowed video sites. The host (and admins on dedicated servers)
  get a "Stop it (host)" button on screens other players use.

## What does not exist yet

Remote desktops (RDP/VNC), direct connections between players (streams always go through the host), giving other
players control of your browser, the in-game host settings panel. Don't look for them.

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
   to Trusted players (Mods menu, Telescreen, When hosting) and the friend tries again.
8. Walk 150+ m away: the screen stops playing on your PC and resumes when you come back (for streams, the sharer's
   HUD line drops to 0 watching).
9. Show on HUD (panel button): picture and sound follow you, also for a shared browser.
10. Save and reload the world: screens come back with their queue and position (shared browsers and windows end).
11. Leave and rejoin; close the game while sharing (the helper should stop with it; check Task Manager for a leftover
    `TelescreenHost.exe` afterwards, there should be none).
12. While sharing, does movement or building lag for the one sharing or the one watching? Tell us your upload speed.

## If something breaks: send these

- `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log` (search for `LogTelescreen`)
- `%LOCALAPPDATA%\FactoryGame\Saved\Logs\TelescreenHost.log` and `TelescreenBrowser.log`
- What you pressed, what you expected, what happened, and whether you were host or client.

## Known issues in this build

- Picture may be mirrored or upside down on a screen (not yet seen in the game). If so: Mods menu, Telescreen,
  Diagnostics, "Mirror picture left-right" / "Flip picture upside down". Please tell us which one fixed it.
- The first video on a PC takes 5-10 seconds to start (YouTube lookup).
- Streams go through the host's upload; a slow upload lowers quality (the stream adapts) and each lost packet
  freezes the picture until the next full frame. Lower "Stream quality (kbps)" in Mods menu, Telescreen, Browser and
  window, if it freezes often.
- A shared window's sound needs Windows 11 or a recent Windows 10; you hear the window yourself as usual, others hear
  it from the screen.
- Only one picture-in-picture at a time; the window can't be dragged or resized yet.
- Clicking the 3D screen directly isn't supported; use the panel and the window.
- The browser's process sandbox is off in this beta.
- YouTube changes things often; if links stop loading, the bundled yt-dlp may need an update.
