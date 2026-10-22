# Telescreen 0.1.0 beta: tester guide

Thanks for testing! This is an early build. Please read "What works" first, so you know what to try and what is
deliberately missing.

## Install (everyone in the session needs the same version)

Requirements: Satisfactory (Steam or Epic, the same game version as the host) and **SML** (Satisfactory Mod Manager
installs it). Windows only: the helper program is Windows only. A dedicated server can run the mod too (server zips
exist) but isn't needed for this test.

1. Download `Telescreen-Windows.zip` from the same place you got this guide.
2. In Satisfactory Mod Manager: pick your profile, **Import local mod**, choose the zip.
   Or by hand: extract the zip's contents into `<Satisfactory>\FactoryGame\Mods\Telescreen\` (create the folder; the
   file `Telescreen.uplugin` must sit directly inside it).
3. The zip is about 270 MB because it carries its own helper programs (video decoder, YouTube downloader, browser).
   **Windows Defender or another antivirus may warn about `TelescreenHost.exe`** (it is a new unsigned program that
   opens a browser and downloads video). It is built from this repository's source; if your antivirus quarantines it,
   allow it for the Telescreen folder.
4. Start the game with the mod. The first start takes a few seconds longer while the helper files are checked.

## What works

- **Screens.** AWESOME Shop, Signs: **Telescreen Set** (25 FICSIT Coupons) unlocks the **Small Telescreen** (8 m x
  4 m) and **Large Telescreen** (16 m x 8 m), built like billboards. They can be rescaled with scaling mods.
- **Synced videos.** Use a screen: paste a YouTube, Twitch or Vimeo link, **Play now** or **Add to queue**. Everyone
  in the session sees and hears the same video at the same moment, with 3D sound from the screen. Pause, seek (-10 s /
  +10 s), next, stop, a shared queue showing who added what, picture fit (letterbox / fill / stretch), screen names.
- **Show on HUD.** A small picture-in-picture of a screen in a corner of your HUD, with its sound everywhere.
- **Browser (single player view only for now).** "Open the browser on this screen" starts a built-in browser that only
  *you* see. F8 opens it in a window you can click and type in; hold Esc or press F8 to go back to the game. F9 lists
  all screens. Web games that lock the mouse work. **Other players do not see your browser yet** (live streaming is
  the next big step); they see "<name> is using the browser".
- **Stream test.** With your browser open on one screen, a second screen's panel offers "Stream test", which shows
  your browser on that screen after encoding and decoding it like a live stream, with the numbers. It only shows on
  your PC.

## What does not exist yet

Live streaming to other players, window capture, remote desktops/VMs, per-player permissions, the host settings
panel. Don't look for them.

## Please test (multiplayer)

Host a world with one friend joining (listen server). Everybody does the same checks; note anything odd.

1. Both can see the Telescreen Set in the AWESOME Shop; the host builds a Small and a Large Telescreen.
2. The host adds a video. The friend (who stands near the screen) should see and hear it within about a second of
   the host. Try different distances: sound should get quieter and come from the screen.
3. The friend joins *while* a video is playing: it should start at the right moment, not from 0.
4. The friend pauses, seeks, skips, adds videos. The host should see all of it, and the queue shows who added what.
5. Link from a site not on the allowed list (e.g. another video site): the host's settings (Mods menu, Telescreen,
   When hosting) decide whether it works. A refusal message should appear in the panel.
6. Walk 150+ m away: the screen stops playing on your PC and resumes in sync when you come back.
7. Show on HUD (panel button): picture and sound follow you.
8. Browser: the friend opens the browser on a screen: the host sees "<friend> is using the browser". Take over,
   Close the browser: the screen goes back to its videos.
9. Save and reload the world: screens come back with their queue and position.
10. Leave and rejoin; close the game with a browser open (the helper should stop with it; check Task Manager for a
    leftover `TelescreenHost.exe` afterwards, there should be none).

## If something breaks: send these

- `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log` (search for `LogTelescreen`)
- `%LOCALAPPDATA%\FactoryGame\Saved\Logs\TelescreenHost.log` and `TelescreenBrowser.log`
- What you pressed, what you expected, what happened, and whether you were host or client.

## Known issues in this build

- Picture may be mirrored or upside down on a screen (not yet seen in the game). If so: Mods menu, Telescreen,
  Diagnostics, "Mirror picture left-right" / "Flip picture upside down". Please tell us which one fixed it.
- The first video on a PC takes 5-10 seconds to start (YouTube lookup).
- Only one picture-in-picture at a time; the window can't be dragged or resized yet.
- Clicking the 3D screen directly isn't supported; use the panel and the window.
- The browser's process sandbox is off in this beta.
- "Only the host can change videos" doesn't apply on dedicated servers yet.
- YouTube changes things often; if links stop loading, the bundled yt-dlp may need an update.
