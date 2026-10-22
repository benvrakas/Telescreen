#pragma once

#include "Common.h"
#include "StreamSession.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Direct stream links between this PC's helper and other players' helpers, over UDP, so live streams don't have to
// pass through the world host. The mod exchanges each side's addresses ("candidates") and a per-link key through the
// game; both sides then send probes to every candidate until one gets through (works through most home routers;
// UPnP or a forwarded port helps the rest). Packets are AES-128-GCM encrypted and authenticated with the link key.
// See Docs/Architecture.md, Direct connections.
class FDirectHub
{
public:
	enum class EState : uint8_t
	{
		Trying = 0,
		Up = 1,
		// Couldn't connect, or stopped hearing from the other side; the stream goes through the game again.
		Down = 2,
		// Receiver: video was lost; ask the sender for a keyframe (through the game).
		NeedKeyframe = 3,
	};

	using FDeliver = std::function<void(uint32_t ReceiverSession, const FStreamPacket& Packet)>;
	using FOnState = std::function<void(uint32_t Session, uint32_t Link, EState State, const std::string& Detail)>;

	FDirectHub(FDeliver InDeliver, FOnState InOnState);
	~FDirectHub();

	// Opens the UDP endpoint once (Port 0: any), maps it with UPnP if asked, learns the public address with STUN, and
	// returns this side's candidates ("ip:port").
	bool Open(uint16_t Port, bool bUpnp, std::vector<std::string>& Candidates, std::string& Error);

	// A link of Session: bSender sends that sender session's packets to the other side; otherwise received packets go
	// to the receiver session.
	void AddPeer(uint32_t Session, uint32_t Link, bool bSender, const uint8_t Key[16], const std::vector<std::string>& Candidates);
	void RemovePeer(uint32_t Link);
	void RemoveSession(uint32_t Session);

	// A sender session's encoded packet: sent on each of its links that are up.
	void Send(uint32_t Session, const FStreamPacket& Packet);

	// Runs two hubs against each other in this process with a file or page source; for --selftest-direct.
	static int SelfTest(double Seconds);

private:
	struct FLink;
	void ReceiveLoop();
	void TickLinks(double Now);
	void HandleDatagram(const uint8_t* Data, int Size, const struct sockaddr_in& From);
	void SendFrame(FLink& Link, uint8_t Type, const uint8_t* Body, size_t Size, const struct sockaddr_in& To);
	bool Stun(std::string& PublicAddress);

	FDeliver Deliver;
	FOnState OnState;
	uintptr_t Socket = ~uintptr_t(0);
	uint16_t BoundPort = 0;
	std::vector<std::string> OwnCandidates;
	bool bMapped = false;
	std::atomic<bool> Stopping{false};
	std::thread Worker;
	std::mutex Mutex;
	std::map<uint32_t, std::unique_ptr<FLink>> Links;
	void* Aes = nullptr;
};
