#pragma once

#include "Common.h"

#include <mutex>

// Server end of the mod's control pipe, using overlapped I/O so reads and writes can run on different threads.
class FPipeServer
{
public:
	~FPipeServer() { Close(); }

	bool Create(const std::wstring& Name);
	bool WaitForClient(DWORD TimeoutMs);

	// Blocks until a whole message arrives; false when the pipe closes.
	bool Read(uint16_t& Type, std::vector<uint8_t>& Payload);

	// Sends one message; safe from any thread.
	bool Send(uint16_t Type, const std::vector<uint8_t>& Payload);

	void Close();

private:
	bool Transfer(bool Write, void* Data, DWORD Size, HANDLE Event);

	HANDLE Pipe = INVALID_HANDLE_VALUE;
	HANDLE ReadEvent = nullptr;
	HANDLE WriteEvent = nullptr;
	std::mutex WriteMutex;
};
