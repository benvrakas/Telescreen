# Remote Desktop mode

A player connects a screen to a computer's remote desktop server: a VM console (QEMU/KVM, Proxmox), a VNC server in
any guest or on another PC (TigerVNC, x11vnc, a Raspberry Pi), and later RDP. The helper on that player's PC is the
client, so nothing is installed on the VM, and the VM's own window state doesn't matter. Like the browser and window
modes, the controller's helper sends the picture and sound to the other players through the stream.

## VNC client (`Helper/src/VncSession.cpp`)

Our own RFB client: libvncclient is GPL, and the mod ships no GPL code.

- **Protocol:** RFB 3.3, 3.7 and 3.8 (the lower of the server's version and 3.8).
- **Security:** None, and VNC Authentication (the 16-byte challenge DES-encrypted in ECB mode with the password,
  whose bytes are bit-reversed; DES from Windows CNG, `bcrypt.lib`). With a password the client picks VNC
  Authentication when offered, otherwise None. Other types (VeNCrypt, RA2, Apple ARD...) are refused with the list
  the server offered. VeNCrypt with TLS is planned; plain VNC isn't encrypted.
- **Pixel format:** 32 bpp, depth 24, little endian, true colour, 8 bits per channel at shifts 16/8/0, so pixels
  arrive as BGRX, the helper's frame format.
- **Encodings,** in preference order: ZRLE (16), Hextile (5), CopyRect (1), Raw (0); pseudo-encodings DesktopSize
  (-223: the framebuffer is reallocated), LastRect (-224), Cursor (-239: the server's pointer image, drawn into the
  published frame at the controller's pointer position) and QEMU Audio (-259, when the session asks for sound).
  ZRLE's zlib stream lasts for the whole connection; inflating uses miniz (`Helper/third_party/miniz`, MIT, 3.0.2).
  ZRLE CPIXELs are 3 bytes (the low bytes of BGRX).
- **Screen shape:** with ExtendedDesktopSize (-308) the client asks once, after the server's first layout, for the
  screen's shape at the stream resolution (SetDesktopSize, message 251, keeping the server's screen id and flags).
  Answers are logged ("done", "not allowed", "invalid layout", "passed on to the guest"). Tested 2026-10-22: QEMU
  11.1's VNC with `-vga std` answers "invalid layout" (it forwards sizes only for displays with UI info, such as
  virtio-gpu); Fill crops such desktops to the screen instead.
- **Updates:** a full FramebufferUpdateRequest after login, then an incremental one after each update. Frames are
  published at most 60 times a second, scaled with swscale's area filter to fit the mod's limit (1920x1080), to the
  session's video buffer (the controller's screen) and the frame taps (the stream sender).
- **Sound:** when the server acknowledges QEMU Audio (a -259 pseudo-rectangle), the client asks for 48 kHz stereo
  signed 16-bit and enables it; samples go to the session's audio ring (the controller hears the VM through the
  screen, since nothing else plays it) and to the audio taps (viewers). Other servers have no sound.
- **Input** is sent over the protocol, so it reaches VMs (Window mode's posted messages don't):
  - Mouse: positions in the picture's pixels are mapped to the desktop's; buttons 1-3; the wheel is clicks of
    buttons 4/5 (vertical) and 6/7 (horizontal).
  - Keys without a character (Enter, Backspace, arrows, F-keys, modifiers, Insert/Delete/Home/End, ...) go as X
    keysyms on key down and up; characters as their keysym (Latin-1 directly, other Unicode as `0x01000000 |
    code point`) down and up; Ctrl+letter arrives from the game as a control character and is sent as the letter.
- **Messages from the server** also handled: SetColourMapEntries and ServerCutText (skipped; extended clipboard's
  negative length too), Bell (ignored), QEMU server messages (audio). Anything else ends the connection with a log
  line.
- **States:** Resolving "Connecting to host:port...", Playing on the first frame, Error with the reason (couldn't
  reach, refused password, unsupported login) or "The VNC server closed the connection."

### Testing without a VNC server

`python Tools/vnc_test_server.py [port] [password] [raw|hextile|zrle] [static]` is a small RFB 3.8 server (None or VNC
Authentication, a moving pattern, a DesktopSize change and a Cursor) that prints the pointer and key events it gets.
`TelescreenHost.exe --selftest-vnc 127.0.0.1:<port> [--password <p>] --seconds 6 [--out frame.bgra]` connects, counts
frames, sends a click, Enter and "hi", and saves the last frame. Verified 2026-10-22: login with and without a
password, all three encodings decode the static pattern with 0 differing pixels, DesktopSize, input events arrive.

Against a real server (2026-10-22): QEMU 11.1.0's VNC server (portable, `D:\SFMod\Tools\VM`, Debian 13 Live XFCE,
WHPX) through a whole boot: VNC password, QEMU audio acknowledged, DesktopSize 640x480 -> 720x400 -> 1280x800, ZRLE at
up to ~20 fps while the desktop drew, the full desktop decoded correctly. That test found the inflate bug below.
`TELESCREEN_VNC_TRACE=1` logs every message and rectangle.

- Fixed: ZRLE rectangles that expand past one 64 KB inflate chunk after the last input byte lost their tail (the
  inflater still held output), so decoding failed and the session ended as if the server had closed it. The loop now
  drains pending output, and decode failures say so instead of "the server closed the connection".

## RDP

Planned through FreeRDP (Apache-2.0): Hyper-V (including the VM console through FreeRDP's Hyper-V connect option),
VirtualBox's VRDE, Windows and xrdp. RDP asks for the screen's shape (dynamic resolution). Not built yet: FreeRDP is
a large dependency to build and ship.
