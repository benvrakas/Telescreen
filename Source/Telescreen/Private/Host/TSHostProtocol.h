#pragma once

// Wire protocol between the Telescreen mod and TelescreenHost.exe. Plain C++ (no engine types): the helper includes it
// too. Layout and message payloads are documented in Docs/HostProtocol.md.

#include <cstdint>

namespace TSHostProtocol
{
	constexpr uint32_t Version = 2;

	enum class EMsg : uint16_t
	{
		Hello = 1,
		HelloAck = 2,
		OpenMedia = 3,
		SyncTarget = 4,
		CloseSession = 5,
		Probe = 6,
		ProbeResult = 7,
		SessionVideo = 8,
		SessionAudio = 9,
		SessionState = 10,
		Shutdown = 11,
		OpenBrowser = 12,
		BrowserInput = 13,
		BrowserCommand = 14,
		BrowserState = 15,
		OpenLoopback = 16,
		OpenStreamSender = 17,
		OpenStreamReceiver = 18,
		StreamPacket = 19,
		StreamControl = 20,
	};

	// StreamControl commands.
	enum class EStreamControl : uint8_t
	{
		Keyframe = 0,
		Bitrate = 1,
		Reset = 2,
	};

	// BrowserInput kinds.
	enum class EInput : uint8_t
	{
		MouseMove = 0,
		MouseButton = 1,
		Wheel = 2,
		Key = 3,
		Text = 4,
		MouseLeave = 5,
		Focus = 6,
		PointerDelta = 7,
	};

	// BrowserInput Key types (as cef_key_event_type_t).
	enum class EKeyType : uint8_t
	{
		RawKeyDown = 0,
		KeyDown = 1,
		KeyUp = 2,
		Char = 3,
	};

	// BrowserCommand kinds.
	enum class ECommand : uint8_t
	{
		Navigate = 0,
		Back = 1,
		Forward = 2,
		Reload = 3,
		Stop = 4,
		Resize = 5,
	};

	// Modifier bits in input messages (as cef_event_flags_t).
	constexpr uint32_t ModShift = 1 << 1;
	constexpr uint32_t ModControl = 1 << 2;
	constexpr uint32_t ModAlt = 1 << 3;
	constexpr uint32_t ModLeftButton = 1 << 4;
	constexpr uint32_t ModMiddleButton = 1 << 5;
	constexpr uint32_t ModRightButton = 1 << 6;

	enum class ESessionState : uint8_t
	{
		Resolving = 0,
		Buffering = 1,
		Playing = 2,
		Paused = 3,
		Ended = 4,
		Error = 5,
	};

#pragma pack(push, 1)
	struct FMsgHeader
	{
		uint32_t PayloadSize;
		uint16_t Type;
		uint16_t Reserved;
	};
#pragma pack(pop)

	constexpr uint32_t MaxPayloadSize = 1u << 20;

	constexpr uint32_t VideoMagic = 0x46565354; // "TSVF"
	constexpr uint32_t AudioMagic = 0x52415354; // "TSAR"

	// Triple-buffer state word: bits 0-1 hold the slot shared between writer and reader, bit 2 marks it as unread.
	constexpr int32_t TripleSlotMask = 3;
	constexpr int32_t TripleFresh = 4;

	// Header at the start of a session's video mapping; three BGRA slots of SlotBytes follow at VideoDataOffset.
	struct alignas(64) FVideoHeader
	{
		uint32_t Magic;
		uint32_t Version;
		uint32_t Width;
		uint32_t Height;
		uint32_t Stride;
		uint32_t SlotBytes;
		volatile int32_t State;
		uint32_t Pad0;
		uint64_t SlotSequence[3];
		double SlotPts[3];
	};
	constexpr uint32_t VideoDataOffset = 256;

	// Header at the start of a session's audio ring; CapacityFrames interleaved float frames follow at AudioDataOffset.
	struct alignas(64) FAudioHeader
	{
		uint32_t Magic;
		uint32_t Version;
		uint32_t SampleRate;
		uint32_t Channels;
		uint32_t CapacityFrames;
		uint32_t Pad0;
		volatile int64_t WriteFrames;
		volatile int64_t ReadFrames;
		volatile int64_t DiscardUntil;
	};
	constexpr uint32_t AudioDataOffset = 256;

	static_assert(sizeof(FVideoHeader) <= VideoDataOffset, "video header too large");
	static_assert(sizeof(FAudioHeader) <= AudioDataOffset, "audio header too large");
}
