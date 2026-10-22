#include "Pipe.h"
#include "Host/TSHostProtocol.h"

bool FPipeServer::Create(const std::wstring& Name)
{
	Pipe = CreateNamedPipeW(Name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1 << 16, 1 << 16, 0, nullptr);
	if (Pipe == INVALID_HANDLE_VALUE)
	{
		LOG_ERROR("CreateNamedPipe(%s) failed: %lu", Narrow(Name).c_str(), GetLastError());
		return false;
	}
	ReadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	WriteEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	return true;
}

bool FPipeServer::WaitForClient(DWORD TimeoutMs)
{
	OVERLAPPED Overlapped{};
	Overlapped.hEvent = ReadEvent;
	ResetEvent(ReadEvent);
	if (ConnectNamedPipe(Pipe, &Overlapped)) return true;
	const DWORD Error = GetLastError();
	if (Error == ERROR_PIPE_CONNECTED) return true;
	if (Error != ERROR_IO_PENDING)
	{
		LOG_ERROR("ConnectNamedPipe failed: %lu", Error);
		return false;
	}
	if (WaitForSingleObject(ReadEvent, TimeoutMs) != WAIT_OBJECT_0)
	{
		CancelIo(Pipe);
		LOG_ERROR("no client connected within %lu ms", TimeoutMs);
		return false;
	}
	DWORD Ignored = 0;
	return GetOverlappedResult(Pipe, &Overlapped, &Ignored, FALSE) != FALSE;
}

bool FPipeServer::Transfer(bool Write, void* Data, DWORD Size, HANDLE Event)
{
	uint8_t* Bytes = static_cast<uint8_t*>(Data);
	while (Size > 0)
	{
		OVERLAPPED Overlapped{};
		Overlapped.hEvent = Event;
		ResetEvent(Event);
		DWORD Done = 0;
		const BOOL Ok = Write ? WriteFile(Pipe, Bytes, Size, nullptr, &Overlapped) : ReadFile(Pipe, Bytes, Size, nullptr, &Overlapped);
		if (!Ok && GetLastError() != ERROR_IO_PENDING) return false;
		if (!GetOverlappedResult(Pipe, &Overlapped, &Done, TRUE) || Done == 0) return false;
		Bytes += Done;
		Size -= Done;
	}
	return true;
}

bool FPipeServer::Read(uint16_t& Type, std::vector<uint8_t>& Payload)
{
	TSHostProtocol::FMsgHeader Header{};
	if (!Transfer(false, &Header, sizeof(Header), ReadEvent)) return false;
	if (Header.PayloadSize > TSHostProtocol::MaxPayloadSize)
	{
		LOG_ERROR("message type %u too large (%u bytes)", Header.Type, Header.PayloadSize);
		return false;
	}
	Type = Header.Type;
	Payload.resize(Header.PayloadSize);
	return Header.PayloadSize == 0 || Transfer(false, Payload.data(), Header.PayloadSize, ReadEvent);
}

bool FPipeServer::Send(uint16_t Type, const std::vector<uint8_t>& Payload)
{
	std::lock_guard<std::mutex> Lock(WriteMutex);
	if (Pipe == INVALID_HANDLE_VALUE) return false;
	TSHostProtocol::FMsgHeader Header{uint32_t(Payload.size()), Type, 0};
	std::vector<uint8_t> Buffer(sizeof(Header) + Payload.size());
	memcpy(Buffer.data(), &Header, sizeof(Header));
	if (!Payload.empty()) memcpy(Buffer.data() + sizeof(Header), Payload.data(), Payload.size());
	return Transfer(true, Buffer.data(), DWORD(Buffer.size()), WriteEvent);
}

void FPipeServer::Close()
{
	if (Pipe != INVALID_HANDLE_VALUE)
	{
		DisconnectNamedPipe(Pipe);
		CloseHandle(Pipe);
		Pipe = INVALID_HANDLE_VALUE;
	}
	if (ReadEvent) CloseHandle(ReadEvent);
	if (WriteEvent) CloseHandle(WriteEvent);
	ReadEvent = WriteEvent = nullptr;
}
