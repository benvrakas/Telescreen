#pragma once

#include "Common.h"
#include "Host/TSHostProtocol.h"

#include <functional>

// Messages a session sends towards the mod.
struct FSessionEvents
{
	std::function<void(uint32_t Session, uint32_t Generation, uint32_t Width, uint32_t Height, const std::string& Mapping)> OnVideo;
	std::function<void(uint32_t Session, uint32_t SampleRate, uint32_t Channels, uint32_t Capacity, const std::string& Mapping)> OnAudio;
	std::function<void(uint32_t Session, TSHostProtocol::ESessionState State, double Position, double Duration, const std::string& Message)> OnState;
	std::function<void(uint32_t Session, const std::string& Url, const std::string& Title, bool Loading, bool CanGoBack, bool CanGoForward,
		uint8_t Cursor, bool PointerLocked)> OnBrowserState;
};

// One media or browser session; the host routes the mod's messages to it.
class FSession
{
public:
	virtual ~FSession() = default;

	// Where synced playback should be now (media sessions only).
	virtual void SetTarget(double MediaSeconds, bool Paused) {}

	// A BrowserInput payload after its session id and kind (browser sessions only).
	virtual void HandleInput(TSHostProtocol::EInput Kind, FPayloadReader& Reader) {}

	// A BrowserCommand payload after its session id and command (browser sessions only).
	virtual void HandleCommand(TSHostProtocol::ECommand Command, FPayloadReader& Reader) {}
};
