#pragma once

#include "Common.h"

#include <mutex>
#include <string>
#include <vector>

// A TLS client on a connected TCP socket, using Windows' own TLS (SChannel). The server's certificate isn't checked
// against Windows' trusted roots (VNC servers use their own certificates); the caller pins its SHA-256 fingerprint
// instead (trust on first use). Receiving and sending may happen on different threads.
class FTlsClient
{
public:
	~FTlsClient();

	// The handshake (blocking). Host is sent for SNI.
	bool Handshake(uintptr_t Socket, const std::string& Host, std::string& Error);

	// Up to Max decrypted bytes (blocks until some arrive): > 0 bytes, 0 the server closed, < 0 an error.
	int Recv(uint8_t* Out, int Max);
	bool SendAll(const uint8_t* Data, size_t Size);
	// Decrypted bytes waiting, so the caller shouldn't wait on the socket.
	bool HasBuffered();

	// The server certificate's SHA-256, as "AB:CD:...", and the protocol ("TLS 1.3").
	const std::string& GetFingerprint() const { return Fingerprint; }
	const std::string& GetProtocol() const { return Protocol; }

private:
	bool ReadRaw();
	bool SendRaw(const void* Data, size_t Size);

	uintptr_t Socket = ~uintptr_t(0);
	void* Cred = nullptr;
	void* Ctx = nullptr;
	bool bHaveContext = false;
	unsigned long HeaderSize = 0, TrailerSize = 0, MaxMessage = 0;
	std::string HostName;
	std::string Fingerprint, Protocol;
	// Received TLS records not decrypted yet, and decrypted bytes not read yet.
	std::vector<uint8_t> Incoming;
	std::vector<uint8_t> Plain;
	size_t PlainPos = 0;
	std::mutex Mutex;
};
