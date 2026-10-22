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
