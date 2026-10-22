#include "Direct.h"
#include "Upnp.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <random>

namespace
{
	constexpr uint32_t Magic = 0x31445354; // "TSD1"
	constexpr size_t HeaderSize = 17;
	constexpr size_t TagSize = 16;
	// Data body: kind u8, flags u8, pts u32, codec u32, packet id u32, chunk index u16, chunk count u16, then the chunk.
	constexpr size_t DataHeaderSize = 18;
	// Stream data per datagram: with headers about 1150 bytes, under common path MTUs.
	constexpr size_t ChunkSize = 1100;
	// Sender pacing per link, so a keyframe doesn't overrun the router or the receiver's buffer.
	constexpr double PaceBytesPerSecond = 4.0e6;
	constexpr double PaceBurstBytes = 64.0 * 1024.0;
	constexpr size_t MaxQueuedBytes = 4u << 20;
	constexpr double ProbeInterval = 0.2, KeepAliveInterval = 1.0;
	constexpr double ConnectTimeout = 12.0, SilenceTimeout = 6.0;

	enum EType : uint8_t
	{
		Probe = 1,
		ProbeAck = 2,
		Data = 3,
		Bye = 4,
	};

	void PutU16(uint8_t* At, uint16_t V) { std::memcpy(At, &V, 2); }
	void PutU32(uint8_t* At, uint32_t V) { std::memcpy(At, &V, 4); }
	void PutU64(uint8_t* At, uint64_t V) { std::memcpy(At, &V, 8); }
	uint16_t GetU16(const uint8_t* At) { uint16_t V; std::memcpy(&V, At, 2); return V; }
	uint32_t GetU32(const uint8_t* At) { uint32_t V; std::memcpy(&V, At, 4); return V; }
	uint64_t GetU64(const uint8_t* At) { uint64_t V; std::memcpy(&V, At, 8); return V; }

	std::string AddressText(const sockaddr_in& Address)
	{
		char Text[64] = {};
		inet_ntop(AF_INET, &Address.sin_addr, Text, sizeof(Text));
		return std::string(Text) + ":" + std::to_string(ntohs(Address.sin_port));
	}

	bool ParseAddress(const std::string& Text, sockaddr_in& Out)
	{
		const size_t Colon = Text.rfind(':');
		if (Colon == std::string::npos) return false;
		Out = sockaddr_in{};
		Out.sin_family = AF_INET;
		const int Port = std::atoi(Text.c_str() + Colon + 1);
		if (Port <= 0 || Port > 65535) return false;
		Out.sin_port = htons(uint16_t(Port));
		return inet_pton(AF_INET, Text.substr(0, Colon).c_str(), &Out.sin_addr) == 1;
	}

	bool SameAddress(const sockaddr_in& A, const sockaddr_in& B)
	{
		return A.sin_port == B.sin_port && A.sin_addr.s_addr == B.sin_addr.s_addr;
	}

	// This PC's IPv4 addresses on adapters that are up (LAN candidates).
	std::vector<std::string> LocalAddresses()
	{
		std::vector<std::string> Result;
		ULONG Size = 16 * 1024;
		std::vector<uint8_t> Buffer(Size);
		auto* Adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(Buffer.data());
		if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, Adapters, &Size) == ERROR_BUFFER_OVERFLOW)
		{
			Buffer.resize(Size);
			Adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(Buffer.data());
			if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, Adapters, &Size) != NO_ERROR) return Result;
		}
		for (IP_ADAPTER_ADDRESSES* Adapter = Adapters; Adapter; Adapter = Adapter->Next)
		{
			if (Adapter->OperStatus != IfOperStatusUp || Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
			for (IP_ADAPTER_UNICAST_ADDRESS* Unicast = Adapter->FirstUnicastAddress; Unicast; Unicast = Unicast->Next)
			{
				const auto* In = reinterpret_cast<const sockaddr_in*>(Unicast->Address.lpSockaddr);
				char Text[64] = {};
				inet_ntop(AF_INET, &In->sin_addr, Text, sizeof(Text));
				if (std::strncmp(Text, "169.254.", 8) != 0) Result.push_back(Text);
			}
		}
		return Result;
	}
}

struct FDirectHub::FLink
{
	uint32_t Session = 0;
	uint32_t Id = 0;
	bool bSender = false;
	BCRYPT_KEY_HANDLE Key = nullptr;
	std::vector<sockaddr_in> Candidates;
	sockaddr_in Remote{};
	bool bHaveRemote = false;
	EState State = EState::Trying;
	double Started = 0.0, LastHeard = 0.0, NextProbe = 0.0, LastLossReport = 0.0;
	// Last packet from the address in use: another working path takes over only when this one goes quiet.
	double LastFromRemote = 0.0;
	uint64_t SendSeq = 0;

	// Sender: encrypted datagrams waiting for the pace.
	std::deque<std::vector<uint8_t>> Queue;
	size_t QueuedBytes = 0;
	double Budget = PaceBurstBytes, BudgetAt = 0.0;
	uint32_t NextPacketId[2] = {0, 0};

	// Receiver: one packet being reassembled per kind.
	struct FPartial
	{
		bool bActive = false;
		uint32_t Id = 0;
		uint16_t Count = 0, Got = 0;
		uint8_t Flags = 0;
		uint32_t PtsMs = 0, Codec = 0;
		std::vector<std::vector<uint8_t>> Parts;
		std::vector<bool> Have;
	};
	FPartial Partial[2];
	bool bHaveLast[2] = {false, false};
	uint32_t LastDelivered[2] = {0, 0};
	bool bWaitKey = false;
};

FDirectHub::FDirectHub(FDeliver InDeliver, FOnState InOnState) : Deliver(std::move(InDeliver)), OnState(std::move(InOnState))
{
	BCRYPT_ALG_HANDLE Alg = nullptr;
	if (BCryptOpenAlgorithmProvider(&Alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0 &&
		BCryptSetProperty(Alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0) == 0)
	{
		Aes = Alg;
	}
	else
	{
		LOG_ERROR("direct: AES-GCM isn't available; direct links are off");
		if (Alg) BCryptCloseAlgorithmProvider(Alg, 0);
	}
}

FDirectHub::~FDirectHub()
{
	Stopping = true;
	if (Worker.joinable()) Worker.join();
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		for (auto& Pair : Links)
		{
			if (Pair.second->Key) BCryptDestroyKey(Pair.second->Key);
		}
		Links.clear();
	}
	if (Socket != ~uintptr_t(0)) closesocket(SOCKET(Socket));
	if (bMapped) Upnp::Unmap();
	if (Aes) BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE(Aes), 0);
}

bool FDirectHub::Stun(std::string& PublicAddress)
{
	for (const char* Server : {"stun.l.google.com:19302", "stun.cloudflare.com:3478"})
	{
		const std::string Text = Server;
		const size_t Colon = Text.rfind(':');
		addrinfo Hints{};
		Hints.ai_family = AF_INET;
		Hints.ai_socktype = SOCK_DGRAM;
		addrinfo* Found = nullptr;
		if (getaddrinfo(Text.substr(0, Colon).c_str(), Text.substr(Colon + 1).c_str(), &Hints, &Found) != 0 || !Found) continue;
		uint8_t Request[20] = {};
		Request[1] = 0x01; // Binding request
		PutU32(Request + 4, htonl(0x2112A442));
		std::mt19937 Random{std::random_device{}()};
		for (int I = 8; I < 20; ++I) Request[I] = uint8_t(Random());
		bool bGot = false;
		for (int Try = 0; Try < 2 && !bGot; ++Try)
		{
			sendto(SOCKET(Socket), reinterpret_cast<const char*>(Request), sizeof(Request), 0, Found->ai_addr, int(Found->ai_addrlen));
			uint8_t Reply[512];
			const double Until = NowSeconds() + 1.0;
			while (!bGot && NowSeconds() < Until)
			{
				sockaddr_in From{};
				int FromLength = sizeof(From);
				const int Got = recvfrom(SOCKET(Socket), reinterpret_cast<char*>(Reply), sizeof(Reply), 0, reinterpret_cast<sockaddr*>(&From), &FromLength);
				if (Got < 20 || Reply[0] != 0x01 || Reply[1] != 0x01 || std::memcmp(Reply + 8, Request + 8, 12) != 0) continue;
				// Attributes: XOR-MAPPED-ADDRESS (0x0020), or MAPPED-ADDRESS (0x0001) from older servers.
				for (int At = 20; At + 4 <= Got;)
				{
					const uint16_t Type = ntohs(GetU16(Reply + At));
					const uint16_t Length = ntohs(GetU16(Reply + At + 2));
					if (At + 4 + Length > Got) break;
					if ((Type == 0x0020 || Type == 0x0001) && Length >= 8 && Reply[At + 5] == 0x01)
					{
						uint16_t Port = ntohs(GetU16(Reply + At + 6));
						uint32_t Address = GetU32(Reply + At + 8);
						if (Type == 0x0020)
						{
							Port ^= 0x2112;
							Address ^= htonl(0x2112A442);
						}
						sockaddr_in Mapped{};
						Mapped.sin_family = AF_INET;
						Mapped.sin_port = htons(Port);
						Mapped.sin_addr.s_addr = Address;
						PublicAddress = AddressText(Mapped);
						bGot = true;
						break;
					}
					At += 4 + ((Length + 3) & ~3);
				}
			}
		}
		freeaddrinfo(Found);
		if (bGot)
		{
			LOG_INFO("direct: STUN %s says this end is %s", Server, PublicAddress.c_str());
			return true;
		}
	}
	LOG_WARN("direct: no STUN server answered (no internet, or UDP blocked)");
	return false;
}

bool FDirectHub::Open(uint16_t Port, bool bUpnp, std::vector<std::string>& Candidates, std::string& Error)
{
	std::lock_guard<std::mutex> OpenLock(Mutex);
	if (!Aes)
	{
		Error = "AES-GCM isn't available";
		return false;
	}
	if (Socket != ~uintptr_t(0))
	{
		Candidates = OwnCandidates;
		return true;
	}
	SOCKET S = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (S == INVALID_SOCKET)
	{
		Error = "couldn't open a UDP socket";
		return false;
	}
	sockaddr_in Bind{};
	Bind.sin_family = AF_INET;
	Bind.sin_port = htons(Port);
	if (bind(S, reinterpret_cast<const sockaddr*>(&Bind), sizeof(Bind)) != 0)
	{
		closesocket(S);
		Error = "UDP port " + std::to_string(Port) + " is in use";
		return false;
	}
	sockaddr_in Bound{};
	int BoundLength = sizeof(Bound);
	getsockname(S, reinterpret_cast<sockaddr*>(&Bound), &BoundLength);
	BoundPort = ntohs(Bound.sin_port);
	int Buffer = 4 << 20;
	setsockopt(S, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&Buffer), sizeof(Buffer));
	setsockopt(S, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&Buffer), sizeof(Buffer));
	DWORD Timeout = 1000;
	setsockopt(S, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&Timeout), sizeof(Timeout));
	Socket = uintptr_t(S);

	auto Add = [this](const std::string& Address)
	{
		if (!Address.empty() && std::find(OwnCandidates.begin(), OwnCandidates.end(), Address) == OwnCandidates.end()) OwnCandidates.push_back(Address);
	};
	std::string Public;
	const bool bStun = Stun(Public);
	if (bUpnp)
	{
		std::string External, UpnpError;
		bMapped = Upnp::MapUdpPort(BoundPort, External, UpnpError);
		if (bMapped && !External.empty()) Add(External + ":" + std::to_string(BoundPort));
		if (!bMapped) LOG_INFO("direct: UPnP: %s", UpnpError.c_str());
	}
	if (bStun)
	{
		Add(Public);
		// A forwarded port keeps its number on the router even when STUN saw another one.
		if (Port != 0) Add(Public.substr(0, Public.rfind(':')) + ":" + std::to_string(Port));
	}
	for (const std::string& Local : LocalAddresses()) Add(Local + ":" + std::to_string(BoundPort));
	Add("127.0.0.1:" + std::to_string(BoundPort));
	u_long NonBlocking = 1;
	ioctlsocket(S, FIONBIO, &NonBlocking);
	Worker = std::thread([this] { ReceiveLoop(); });
	std::string List;
	for (const std::string& Each : OwnCandidates) List += (List.empty() ? "" : ", ") + Each;
	LOG_INFO("direct: UDP %u open; candidates %s", BoundPort, List.c_str());
	Candidates = OwnCandidates;
	return true;
}

void FDirectHub::AddPeer(uint32_t Session, uint32_t Link, bool bSender, const uint8_t Key[16], const std::vector<std::string>& Candidates)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (!Aes || Socket == ~uintptr_t(0)) return;
	auto New = std::make_unique<FLink>();
	New->Session = Session;
	New->Id = Link;
	New->bSender = bSender;
	if (BCryptGenerateSymmetricKey(BCRYPT_ALG_HANDLE(Aes), &New->Key, nullptr, 0, const_cast<PUCHAR>(Key), 16, 0) != 0)
	{
		LOG_WARN("direct: link %u: couldn't make its key", Link);
		return;
	}
	for (const std::string& Text : Candidates)
	{
		sockaddr_in Address{};
		if (ParseAddress(Text, Address) && New->Candidates.size() < 16) New->Candidates.push_back(Address);
	}
	New->Started = NowSeconds();
	if (auto It = Links.find(Link); It != Links.end() && It->second->Key) BCryptDestroyKey(It->second->Key);
	LOG_INFO("direct: link %u for session %u (%s), %zu candidate(s)", Link, Session, bSender ? "sending" : "receiving", New->Candidates.size());
	Links[Link] = std::move(New);
}

void FDirectHub::RemovePeer(uint32_t Link)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	auto It = Links.find(Link);
	if (It == Links.end()) return;
	if (It->second->bHaveRemote) SendFrame(*It->second, Bye, nullptr, 0, It->second->Remote);
	if (It->second->Key) BCryptDestroyKey(It->second->Key);
	Links.erase(It);
	LOG_INFO("direct: link %u removed", Link);
}

void FDirectHub::RemoveSession(uint32_t Session)
{
	std::vector<uint32_t> Ids;
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		for (const auto& Pair : Links)
		{
			if (Pair.second->Session == Session) Ids.push_back(Pair.first);
		}
	}
	for (uint32_t Id : Ids) RemovePeer(Id);
}

void FDirectHub::SendFrame(FLink& Link, uint8_t Type, const uint8_t* Body, size_t Size, const sockaddr_in& To)
{
	std::vector<uint8_t> Packet(HeaderSize + Size + TagSize);
	PutU32(Packet.data(), Magic);
	PutU32(Packet.data() + 4, Link.Id);
	PutU64(Packet.data() + 8, ++Link.SendSeq);
	Packet[16] = Type;
	// The nonce: direction (sender 0, receiver 1) and this side's sequence number, never reused under one key.
	uint8_t Nonce[12] = {};
	Nonce[0] = Link.bSender ? 0 : 1;
	PutU64(Nonce + 4, Link.SendSeq);
	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO Info;
	BCRYPT_INIT_AUTH_MODE_INFO(Info);
	Info.pbNonce = Nonce;
	Info.cbNonce = sizeof(Nonce);
	Info.pbAuthData = Packet.data();
	Info.cbAuthData = ULONG(HeaderSize);
	Info.pbTag = Packet.data() + HeaderSize + Size;
	Info.cbTag = ULONG(TagSize);
	ULONG Written = 0;
	if (BCryptEncrypt(Link.Key, const_cast<PUCHAR>(Body), ULONG(Size), &Info, nullptr, 0, Packet.data() + HeaderSize, ULONG(Size), &Written, 0) != 0) return;
	if (Type == Data)
	{
		Link.QueuedBytes += Packet.size();
		Link.Queue.push_back(std::move(Packet));
		return;
	}
	sendto(SOCKET(Socket), reinterpret_cast<const char*>(Packet.data()), int(Packet.size()), 0, reinterpret_cast<const sockaddr*>(&To), sizeof(To));
}

void FDirectHub::Send(uint32_t Session, const FStreamPacket& Packet)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	for (auto& Pair : Links)
	{
		FLink& Link = *Pair.second;
		if (Link.Session != Session || !Link.bSender || Link.State != EState::Up) continue;
		const int Kind = Packet.Kind == EStreamKind::Audio ? 1 : 0;
		const uint32_t PacketId = Link.NextPacketId[Kind]++;
		const size_t Count = std::max<size_t>(1, (Packet.Data.size() + ChunkSize - 1) / ChunkSize);
		if (Count > 0xFFFF) continue;
		if (Link.QueuedBytes > MaxQueuedBytes)
		{
			// Too far behind: what's queued is stale; the receiver notices the gap and asks for a keyframe.
			Link.Queue.clear();
			Link.QueuedBytes = 0;
		}
		std::vector<uint8_t> Body(DataHeaderSize + ChunkSize);
		for (size_t Index = 0; Index < Count; ++Index)
		{
			const size_t Offset = Index * ChunkSize;
			const size_t Length = std::min(ChunkSize, Packet.Data.size() - std::min(Offset, Packet.Data.size()));
			Body[0] = uint8_t(Kind);
			Body[1] = Packet.Flags;
			PutU32(Body.data() + 2, Packet.PtsMs);
			PutU32(Body.data() + 6, Packet.Codec);
			PutU32(Body.data() + 10, PacketId);
			PutU16(Body.data() + 14, uint16_t(Index));
			PutU16(Body.data() + 16, uint16_t(Count));
			Body.resize(DataHeaderSize + Length);
			if (Length > 0) std::memcpy(Body.data() + DataHeaderSize, Packet.Data.data() + Offset, Length);
			SendFrame(Link, Data, Body.data(), Body.size(), Link.Remote);
		}
	}
}

void FDirectHub::ReceiveLoop()
{
	std::vector<uint8_t> Buffer(2048);
	while (!Stopping)
	{
		fd_set Read;
		FD_ZERO(&Read);
		FD_SET(SOCKET(Socket), &Read);
		timeval Wait{0, 2000};
		if (select(0, &Read, nullptr, nullptr, &Wait) > 0)
		{
			for (int I = 0; I < 256; ++I)
			{
				sockaddr_in From{};
				int FromLength = sizeof(From);
				const int Got = recvfrom(SOCKET(Socket), reinterpret_cast<char*>(Buffer.data()), int(Buffer.size()), 0, reinterpret_cast<sockaddr*>(&From),
					&FromLength);
				if (Got <= 0) break;
				HandleDatagram(Buffer.data(), Got, From);
			}
		}
		TickLinks(NowSeconds());
	}
}

void FDirectHub::HandleDatagram(const uint8_t* Bytes, int Size, const sockaddr_in& From)
{
	if (Size < int(HeaderSize + TagSize) || GetU32(Bytes) != Magic) return;
	std::vector<std::pair<uint32_t, FStreamPacket>> Deliveries;
	std::vector<std::tuple<uint32_t, uint32_t, EState, std::string>> States;
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		auto It = Links.find(GetU32(Bytes + 4));
		if (It == Links.end()) return;
		FLink& Link = *It->second;
		const uint64_t Seq = GetU64(Bytes + 8);
		const uint8_t Type = Bytes[16];
		const size_t BodySize = size_t(Size) - HeaderSize - TagSize;
		std::vector<uint8_t> Body(BodySize);
		uint8_t Nonce[12] = {};
		Nonce[0] = Link.bSender ? 1 : 0;
		PutU64(Nonce + 4, Seq);
		BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO Info;
		BCRYPT_INIT_AUTH_MODE_INFO(Info);
		Info.pbNonce = Nonce;
		Info.cbNonce = sizeof(Nonce);
		Info.pbAuthData = const_cast<PUCHAR>(Bytes);
		Info.cbAuthData = ULONG(HeaderSize);
		Info.pbTag = const_cast<PUCHAR>(Bytes + HeaderSize + BodySize);
		Info.cbTag = ULONG(TagSize);
		ULONG Written = 0;
		if (BCryptDecrypt(Link.Key, const_cast<PUCHAR>(Bytes + HeaderSize), ULONG(BodySize), &Info, nullptr, 0, Body.data(), ULONG(BodySize), &Written, 0) != 0)
		{
			return;
		}
		// Authenticated: this is the other side, wherever its packets come from now.
		const double Now = NowSeconds();
		Link.LastHeard = Now;
		if (Link.bHaveRemote && SameAddress(Link.Remote, From))
		{
			Link.LastFromRemote = Now;
		}
		else if (!Link.bHaveRemote || Now - Link.LastFromRemote > 2.0)
		{
			LOG_INFO("direct: link %u reaches the other side at %s", Link.Id, AddressText(From).c_str());
			Link.Remote = From;
			Link.bHaveRemote = true;
			Link.LastFromRemote = Now;
		}
		if (Type == Probe)
		{
			SendFrame(Link, ProbeAck, nullptr, 0, From);
		}
		if ((Type == Probe || Type == ProbeAck || Type == Data) && Link.State == EState::Trying)
		{
			Link.State = EState::Up;
			States.emplace_back(Link.Session, Link.Id, EState::Up, AddressText(From));
		}
		if (Type == Bye && Link.State != EState::Down)
		{
			Link.State = EState::Down;
			States.emplace_back(Link.Session, Link.Id, EState::Down, "the other side closed the link");
		}
		if (Type == Data && !Link.bSender && BodySize >= DataHeaderSize)
		{
			const int Kind = Body[0] & 1;
			const uint32_t PacketId = GetU32(Body.data() + 10);
			const uint16_t Index = GetU16(Body.data() + 14);
			const uint16_t Count = GetU16(Body.data() + 16);
			FLink::FPartial& Part = Link.Partial[Kind];
			bool bLoss = false;
			if (Count == 0 || Index >= Count || (Link.bHaveLast[Kind] && int32_t(PacketId - Link.LastDelivered[Kind]) <= 0)) return;
			if (!Part.bActive || Part.Id != PacketId)
			{
				// A newer packet while one is incomplete, or a skipped id: video was lost.
				if ((Part.bActive && int32_t(PacketId - Part.Id) > 0) || (Link.bHaveLast[Kind] && PacketId != Link.LastDelivered[Kind] + 1)) bLoss = Kind == 0;
				Part = FLink::FPartial();
				Part.bActive = true;
				Part.Id = PacketId;
				Part.Count = Count;
				Part.Flags = Body[1];
				Part.PtsMs = GetU32(Body.data() + 2);
				Part.Codec = GetU32(Body.data() + 6);
				Part.Parts.resize(Count);
				Part.Have.assign(Count, false);
			}
			if (Count == Part.Count && !Part.Have[Index])
			{
				Part.Parts[Index].assign(Body.begin() + DataHeaderSize, Body.end());
				Part.Have[Index] = true;
				++Part.Got;
			}
			if (bLoss)
			{
				Link.bWaitKey = true;
				if (Now - Link.LastLossReport > 0.5)
				{
					Link.LastLossReport = Now;
					States.emplace_back(Link.Session, Link.Id, EState::NeedKeyframe, std::string());
				}
			}
			if (Part.Got == Part.Count)
			{
				FStreamPacket Packet;
				Packet.Kind = Kind == 1 ? EStreamKind::Audio : EStreamKind::Video;
				Packet.Flags = Part.Flags;
				Packet.PtsMs = Part.PtsMs;
				Packet.Codec = Part.Codec;
				for (auto& Piece : Part.Parts) Packet.Data.insert(Packet.Data.end(), Piece.begin(), Piece.end());
				Link.bHaveLast[Kind] = true;
				Link.LastDelivered[Kind] = Part.Id;
				Part = FLink::FPartial();
				const bool bKey = (Packet.Flags & StreamFlagKey) != 0;
				if (Kind == 0 && Link.bWaitKey && !bKey) return;
				if (Kind == 0 && bKey) Link.bWaitKey = false;
				Deliveries.emplace_back(Link.Session, std::move(Packet));
			}
		}
	}
	for (auto& [Session, Packet] : Deliveries)
	{
		if (Deliver) Deliver(Session, Packet);
	}
	for (auto& [Session, Id, State, Detail] : States)
	{
		if (OnState) OnState(Session, Id, State, Detail);
	}
}

void FDirectHub::TickLinks(double Now)
{
	std::vector<std::tuple<uint32_t, uint32_t, EState, std::string>> States;
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		for (auto& Pair : Links)
		{
			FLink& Link = *Pair.second;
			if (Link.State == EState::Down) continue;
			if (Now >= Link.NextProbe)
			{
				if (Link.State == EState::Trying)
				{
					Link.NextProbe = Now + ProbeInterval;
					const uint8_t Role = Link.bSender ? 0 : 1;
					for (const sockaddr_in& To : Link.Candidates) SendFrame(Link, Probe, &Role, 1, To);
					if (Link.bHaveRemote) SendFrame(Link, Probe, &Role, 1, Link.Remote);
				}
				else
				{
					Link.NextProbe = Now + KeepAliveInterval;
					const uint8_t Role = Link.bSender ? 0 : 1;
					SendFrame(Link, Probe, &Role, 1, Link.Remote);
				}
			}
			if (Link.State == EState::Trying && Now - Link.Started > ConnectTimeout)
			{
				Link.State = EState::Down;
				States.emplace_back(Link.Session, Link.Id, EState::Down, "couldn't reach the other side");
			}
			else if (Link.State == EState::Up && Now - Link.LastHeard > SilenceTimeout)
			{
				Link.State = EState::Down;
				States.emplace_back(Link.Session, Link.Id, EState::Down, "lost the other side");
			}
			// Paced sending of queued stream data.
			Link.Budget = std::min(PaceBurstBytes, Link.Budget + (Now - Link.BudgetAt) * PaceBytesPerSecond);
			Link.BudgetAt = Now;
			while (!Link.Queue.empty() && Link.State == EState::Up && Link.Budget >= double(Link.Queue.front().size()))
			{
				const std::vector<uint8_t>& Packet = Link.Queue.front();
				if (sendto(SOCKET(Socket), reinterpret_cast<const char*>(Packet.data()), int(Packet.size()), 0, reinterpret_cast<const sockaddr*>(&Link.Remote),
						sizeof(Link.Remote)) < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
				{
					break;
				}
				Link.Budget -= double(Packet.size());
				Link.QueuedBytes -= Packet.size();
				Link.Queue.pop_front();
			}
		}
	}
	for (auto& [Session, Id, State, Detail] : States)
	{
		LOG_INFO("direct: link %u %s (%s)", Id, State == EState::Down ? "down" : "changed", Detail.c_str());
		if (OnState) OnState(Session, Id, State, Detail);
	}
}

int FDirectHub::SelfTest(double Seconds)
{
	std::atomic<int> Delivered{0}, Keyframes{0}, Lost{0};
	std::atomic<uint64_t> Bytes{0};
	std::atomic<bool> SenderUp{false}, ReceiverUp{false};
	FDirectHub Sender([](uint32_t, const FStreamPacket&) {}, [&](uint32_t, uint32_t, EState State, const std::string& Detail)
	{
		if (State == EState::Up) { SenderUp = true; LOG_INFO("selftest: sender link up (%s)", Detail.c_str()); }
		if (State == EState::Down) LOG_INFO("selftest: sender link down (%s)", Detail.c_str());
	});
	FDirectHub Receiver([&](uint32_t, const FStreamPacket& Packet)
	{
		++Delivered;
		Bytes += Packet.Data.size();
		if (Packet.Flags & StreamFlagKey) ++Keyframes;
	}, [&](uint32_t, uint32_t, EState State, const std::string& Detail)
	{
		if (State == EState::Up) { ReceiverUp = true; LOG_INFO("selftest: receiver link up (%s)", Detail.c_str()); }
		if (State == EState::NeedKeyframe) ++Lost;
		if (State == EState::Down) LOG_INFO("selftest: receiver link down (%s)", Detail.c_str());
	});
	std::vector<std::string> SenderCandidates, ReceiverCandidates;
	std::string Error;
	if (!Sender.Open(0, false, SenderCandidates, Error) || !Receiver.Open(0, false, ReceiverCandidates, Error))
	{
		LOG_ERROR("selftest: %s", Error.c_str());
		return 1;
	}
	uint8_t Key[16];
	std::mt19937 Random{std::random_device{}()};
	for (uint8_t& Byte : Key) Byte = uint8_t(Random());
	Sender.AddPeer(1, 77, true, Key, ReceiverCandidates);
	Receiver.AddPeer(2, 77, false, Key, SenderCandidates);
	const double Start = NowSeconds();
	while (!(SenderUp && ReceiverUp) && NowSeconds() - Start < 5.0) Sleep(10);
	LOG_INFO("selftest: link up after %.2f s", NowSeconds() - Start);
	// 30 packets a second at about 3 Mbps: a 120 KB keyframe every 2 s, 10 KB frames between.
	int Sent = 0;
	const double SendStart = NowSeconds();
	while (NowSeconds() - SendStart < Seconds)
	{
		FStreamPacket Packet;
		Packet.Kind = EStreamKind::Video;
		const bool bKey = Sent % 60 == 0;
		Packet.Flags = bKey ? StreamFlagKey : 0;
		Packet.PtsMs = uint32_t(Sent * 33);
		Packet.Data.resize(bKey ? 120000 : 10000, uint8_t(Sent));
		Sender.Send(1, Packet);
		++Sent;
		Sleep(33);
	}
	Sleep(300);
	LOG_INFO("selftest result: sent %d, delivered %d (%d keyframes), %.1f MB, %d loss report(s)", Sent, Delivered.load(), Keyframes.load(), Bytes / 1.0e6, Lost.load());
	return Delivered.load() >= Sent * 9 / 10 ? 0 : 1;
}
