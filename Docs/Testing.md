# In-game test checklist

## M0 / M1 (single player)

1. Game start: the log has `Telescreen content resolved` with no `MISSING`, and `Runtime components verified ... host
   ok, fetcher ok`.
2. AWESOME Shop, Signs: "Telescreen Set" costs 25 coupons; buying it unlocks Small and Large Telescreen.
3. Build menu, Organization, Signs: both screens appear with the billboard icons. The hologram snaps to walls and
   gets a pole on the ground like a billboard. Building takes the billboard's materials.
4. The built screen shows a dark status card: "TELESCREEN", the screen's name, "Interact to add a video". If the text
   is mirrored or upside down, note which (Mods menu, Telescreen, Diagnostics has fixes, but the default should be
   right).
5. Use the screen: the panel opens; Esc and Close close it.
6. Paste `https://www.youtube.com/watch?v=aqz-KE-bpKQ`, Play now: "Looking up the link..." then "Added ...". The screen
   shows Loading..., then the video with sound from the screen (louder close, fading with distance).
7. Play / Pause, -10 s / +10 s, Next, Stop, queue Play / Remove / Clear, Letterbox / Fill / Stretch, Save name.
8. "Show on HUD": a small picture of the screen appears in the bottom-right corner without taking the mouse; its
   sound plays everywhere; it keeps playing when you walk away. "Remove from HUD" (or dismantling the screen) removes it.
   Size, corner and 2D/3D sound are in the Mods menu, Telescreen, Picture on the HUD.
9. Walk past the play distance (150 m): the screen pauses on your PC and shows its status; walking back resumes in sync.
10. Save, reload: the queue and position come back (playing items continue from the saved position).
11. Rescale a screen with Infinite Nudge: the picture keeps the right shape (letterbox bars change).

## M2 (browser, single player)

1. Panel, Browser: "Open the browser on this screen" with an empty address: the screen shows the start page
   (DuckDuckGo) at the screen's shape. Typing an address + Go, Back, Forward, Reload work.
2. After opening the browser (and, the first time, confirming the dialog) its window opens by itself, centred with the
   game dimmed behind it; "Use it in a window" in the panel header opens it again; the mouse cursor appears; clicking, scrolling, typing (including
   Ctrl+V paste) work in the page; the cursor changes over links and text boxes.
3. Esc reaches the page (e.g. closes a site's menu); holding Esc for a second, or F8, returns to the game; F8 again
   opens the window again.
4. A browser game that locks the mouse (e.g. a first-person game on itch.io): clicking locks it, the mouse turns the
   view, Esc releases it.
5. Page sound comes from the screen (or plays everywhere while the window is open), not straight from Windows.
6. F9 opens the screens list: names, distances, what's showing; Show on HUD and Open window work; Esc closes it.
7. "Close the browser": the screen goes back to its video queue.
8. Blocked: `chrome://version` and `file:///C:/` in the address box don't open; a page asking for the microphone gets
   refused; a download link does nothing.

## M3 (stream test)

1. Open the browser on one screen, then open a second screen's panel: "Stream test" appears. Pressing it shows your
   browser on the second screen after encoding and decoding, with the encoder, bitrate and timings under the button.
   Judge text sharpness and motion (e.g. scroll a page, play a video).
2. Pressing it again stops the test; closing the browser stops it too.

## M4 (live streaming, multiplayer)

1. A opens the browser on a screen: the first time, the sharing notice appears and the second press opens it. The
   panel says "No one is watching your browser yet."
2. B walks up to the screen: within a couple of seconds B sees A's browser and hears its sound from the screen. A's
   panel says "Streaming to 1 player(s)" with the encoder and rate; the window title says "1 watching".
3. A scrolls, plays a video and a browser game: B sees it at about 30 fps and a fraction of a second behind.
4. A presses F10: B sees "They paused the stream" while A keeps using the browser; F10 again brings it back for B.
5. B walks out of play distance: A's count drops to 0 and the stream stops (A's log: `stopped streaming`); walking
   back starts it again. B can also Show on HUD to keep watching anywhere.
6. B takes over: A becomes the viewer of B's browser.
7. Host turns off "Allow sharing browsers": B gets "Sharing browsers is turned off on this world." and only sees who
   is using the screen. B's own "Watch other players' browsers" = 1 does the same on B's PC only.
8. Dedicated server: same as 2 and 3; the server log shows the watch lines and the raised net rate.
9. Note in the report: both players' upload and download speeds, the controller's GPU, what B saw (freezes, blur),
   and whether movement lagged for A while streaming.

## M5 (window sharing) and roles

1. Panel, Share a window tab: the list shows your open windows (not the game). Share this window: the first time a
   dialog asks to confirm; I understand, continue. The screen shows the window (scaled, letterboxed to its shape); the HUD line
   says "Sharing a window".
2. A friend sees it and hears the window's sound from the screen; you hear it from the window as usual.
3. F8: clicks and typing in the window reach a normal app (e.g. Notepad).
4. Minimise the window: your screen says to restore it; restore: it continues. Close the window: the screen goes
   back to its videos with a notice.
5. Roles: as a client with default host settings, Share gives "Only trusted players can share a window"; with your
   name in Trusted players it works. "Who can use the browser" = 2 blocks clients from the browser.
6. "Players can take over" off: a client can't take over the host's browser; the host's panel shows "Stop it (host)"
   on a client's screen and it works.
7. Taking control of a second screen releases your first one.
8. Dedicated server: an admin (server manager login) counts as host for the settings above.

## M1 multiplayer

1. Listen server: a client sees and hears the same video, within about a quarter of a second of the host.
2. A client joining mid-video starts at the right position.
3. A client adds a video from a site not in the host's allowed list: the panel shows the refusal.
4. Privacy: with "Load videos other players add" = 2, a video added by someone else shows its title and
   "Not loaded on this PC" with a "Load it anyway" button in the panel.
5. Dedicated server: same as 1 and 2; the server log has the `Server_Enqueue` and command lines and no helper.
6. Browser (M2): when a client opens the browser, others see it (M4); Take over moves control; when the controller
   leaves the game, the screen goes back to its videos.

## Stream settings (beta 10)

Outside the game, the helper's stream self-test runs a browser through sender and receiver:

```
TelescreenHost.exe --selftest-stream https://example.com --browserdir Resources\Runtime\browser --browserdata <dir>
  [--stream-height 480] [--prefer av1|hevc|h264|software] [--kbps 1500] --seconds 6 --log <file>
```

Verified 2026-10-22: `--stream-height 480 --prefer h264` encodes the 1280x720 browser as h264_nvenc 852x480 at 30 fps
(166 packets, none dropped); `--prefer hevc` picks hevc_nvenc. In game: change Stream resolution / bitrate / encoder
while sharing (the sharer's log shows "stream settings changed; restarting the stream"); the world host lowers the
highest resolution and the sharer's stream follows within a few seconds.

## Subtitles and direct connections (1.0)

```
TelescreenHost.exe --selftest <url> --ytdlp <yt-dlp.exe> --jsruntime <qjs.exe> --subtitles 1 --seconds 12 --log <file>
TelescreenHost.exe --selftest-file <video> --subtitles 1 --seconds 11 --log <file>
TelescreenHost.exe --selftest-direct 6 --log <file>
```

`--subtitles N` selects track N 8 s in and logs the tracks and cues; `--subtitle-language` sets the preferred language.
Verified 2026-10-22: a YouTube video lists English (Original) and English automatic captions (json3), 1048 cues in
0.3 s; a .srt next to an MKV (tags removed); an SRT track inside an MKV, read from the whole file. `--selftest-direct`
runs two direct-link hubs against each other (see Docs/Architecture.md, Direct connections). Not tested yet: UPnP
port mapping on a real router, direct links between two PCs on different networks.
