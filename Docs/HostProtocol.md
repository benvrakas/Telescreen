# TelescreenHost protocol

`TelescreenHost.exe` is the helper process that does all media work for one game client. The mod starts it with

```
TelescreenHost.exe --pipe \\.\pipe\Telescreen-<gamePid> --parent <gamePid> --log <file> --ytdlp <path>
```

and talks to it over that named pipe. The helper exits when the pipe closes or the game process ends; the mod also
puts it in a job object that kills it with the game. Constants and shared-memory structs:
`Source/Telescreen/Private/Host/TSHostProtocol.h`.

## Pipe framing

Every message is an `FMsgHeader` (payload size, type) followed by the payload. Payload fields are little-endian:
`u8`, `u32`, `f64`, and `str` = `u32` byte length + UTF-8 bytes. Both ends use overlapped I/O, because synchronous
handles serialize reads and writes on one pipe.

| Type | Direction | Payload |
|---|---|---|
| Hello | mod → host | u32 version |
| HelloAck | host → mod | u32 version, str host version, u32 host pid |
| OpenMedia | mod → host | u32 session, str url, u8 is local file, f64 start seconds, u8 paused, u32 max width, u32 max height |
| SyncTarget | mod → host | u32 session, f64 media seconds (where playback should be now), u8 paused |
| CloseSession | mod → host | u32 session |
| Probe | mod → host | u32 request, str url |
| ProbeResult | host → mod | u32 request, u8 ok, str title, f64 duration (0 unknown), u8 is live, str error |
| SessionVideo | host → mod | u32 session, u32 generation, u32 width, u32 height, str mapping name |
| SessionAudio | host → mod | u32 session, u32 sample rate, u32 channels, u32 capacity frames, str mapping name |
| SessionState | host → mod | u32 session, u8 state, f64 position, f64 duration, str message |
| Shutdown | mod → host | (empty) |
| OpenBrowser | mod → host | u32 session, str url, u32 width, u32 height, u8 keep logins, str profile name |
| BrowserInput | mod → host | u32 session, u8 kind, then by kind: MouseMove: i32 x, i32 y, u32 modifiers; MouseButton: i32 x, i32 y, u8 button (0 left, 1 middle, 2 right), u8 up, u8 click count, u32 modifiers; Wheel: i32 x, i32 y, i32 dx, i32 dy, u32 modifiers; Key: u8 type, i32 Windows key code, i32 native key code, u16 character, u32 modifiers; Text: str; MouseLeave: none; Focus: u8 focused; PointerDelta: i32 dx, i32 dy (raw mouse movement while the page holds the pointer lock) |
| BrowserCommand | mod → host | u32 session, u8 command, then Navigate: str url; Resize: u32 width, u32 height |
| OpenLoopback | mod → host | u32 session, u32 source session, u32 fps, u32 kbps, str preferred encoder (M3 test: the source's frames through the stream encoder and decoder) |
| OpenStreamSender | mod → host | u32 session, u32 source session, u32 fps, u32 kbps, u8 allow software encoding, str preferred encoder (empty, a family `av1`/`hevc`/`h264`/`software`, or an FFmpeg encoder name), u32 max height (0: the source size; taller frames are scaled down keeping their shape) |
| OpenStreamReceiver | mod → host | u32 session |
| StreamPacket | both | u32 session, u8 kind (0 video, 1 audio), u8 flags (1 keyframe), u32 pts ms, u32 codec (AVCodecID), then the packet bytes to the end of the payload. Host → mod from senders, mod → host for receivers |
| StreamControl | mod → host | u32 session, u8 command (0 keyframe, 1 bitrate kbps, 2 reset: drop video until the next keyframe), u32 value |
| BrowserState | host → mod | u32 session, str url, str title, u8 loading, u8 can go back, u8 can go forward, u8 cursor type (cef_cursor_type_t), u8 pointer locked |
| OpenWindow | mod → host | u32 session, u32 window handle low, u32 window handle high, u8 include cursor, u8 capture sound, u32 max width, u32 max height |
| ListWindows | mod → host | u32 request |
| WindowList | host → mod | u32 request, u32 count, then per window: u32 handle low, u32 handle high, u32 process id, str title, str exe name |

Protocol version 3 added the three window messages.

Window sessions take BrowserInput (mouse, wheel, key, text; positions in the session's frame pixels) and post it to
the window; their sound goes only to the frame and audio taps (the stream sender), not to a SessionAudio ring,
because the controller already hears the window directly. SessionState Buffering with a message means the window is
minimised; Error means it closed or can't be captured.

CEF's off-screen mode rejects `requestPointerLock()`, so the helper injects a small script into every page before
its own scripts run (DevTools `Page.addScriptToEvaluateOnNewDocument`). It stands in for the Pointer Lock API:
`pointerLockElement`, `requestPointerLock`, `exitPointerLock`, `pointerlockchange`, Escape to leave. It tells the
helper through a tagged console message, which becomes the "pointer locked" flag in BrowserState. While it is set,
the mod sends PointerDelta instead of absolute moves, and the page gets them as `mousemove` events with
`movementX/Y`, the way browser games read the mouse. These events are untrusted (`isTrusted` false), which games
don't check.

Browser sessions use the same SessionVideo / SessionAudio / SessionState messages; their audio is live (no clock
or sync target), and the browser's coordinates are the pixels of its video frames.

## Video: triple buffer in shared memory

`FVideoHeader` then three BGRA slots. The writer owns one slot (back), the reader one (front), and the state word
holds the third (middle) plus a fresh bit. Writer: fill back, then `back = Exchange(State, back | Fresh) & 3`.
Reader: if `State & Fresh`, `front = Exchange(State, front) & 3`, then read front. No locks, the reader always
gets the newest complete frame, and the writer never waits. A size change creates a new mapping (new generation
in `SessionVideo`).

## Audio: ring in shared memory

`FAudioHeader` then `CapacityFrames` interleaved float frames (48 kHz stereo). Single writer (host), single reader
(the mod's audio thread). `WriteFrames` / `ReadFrames` count frames ever written / read; the writer publishes
`WriteFrames` after copying, the reader publishes `ReadFrames` after copying. After a seek the writer sets
`DiscardUntil`, and the reader skips forward to it.

## Clock and sync

The host's playback clock follows the audio the mod has consumed (`ReadFrames` mapped back to media time), so the
picture stays in step with what is heard. Without audio, or when the mod stops pulling audio for over a second, it
falls back to a wall clock. The mod sends `SyncTarget` with the media time playback should be at, from the
replicated start time and the server clock. The host seeks when it is more than 1.5 s off, and otherwise stretches
or squeezes audio by up to 5% (`swr_set_compensation`) until it is within 40 ms.
