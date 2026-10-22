#include "Upnp.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>

#include <mutex>
#include <vector>

namespace
{
	struct FGateway
	{
		std::string ControlUrl;
		std::string ServiceType;
		std::string LocalIp;
	};

	std::mutex GUpnpMutex;
	FGateway GMapped;
	uint16_t GMappedPort = 0;

	std::string Between(const std::string& Text, const std::string& Open, const std::string& Close, size_t From = 0)
	{
		const size_t Start = Text.find(Open, From);
		if (Start == std::string::npos) return std::string();
		const size_t End = Text.find(Close, Start + Open.size());
		return End == std::string::npos ? std::string() : Text.substr(Start + Open.size(), End - Start - Open.size());
	}

	// Asks the network for Internet Gateway Devices (SSDP) and returns their description URLs.
	std::vector<std::string> Discover()
	{
		std::vector<std::string> Locations;
		SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (Socket == INVALID_SOCKET) return Locations;
		DWORD Timeout = 1500;
		setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&Timeout), sizeof(Timeout));
		sockaddr_in To{};
		To.sin_family = AF_INET;
		To.sin_port = htons(1900);
		inet_pton(AF_INET, "239.255.255.250", &To.sin_addr);
		for (const char* Target : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1"})
		{
			const std::string Request = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ") + Target + "\r\n\r\n";
			sendto(Socket, Request.data(), int(Request.size()), 0, reinterpret_cast<const sockaddr*>(&To), sizeof(To));
		}
		char Buffer[2048];
		const double Until = NowSeconds() + 1.5;
		while (NowSeconds() < Until)
		{
			const int Got = recv(Socket, Buffer, sizeof(Buffer) - 1, 0);
			if (Got <= 0) break;
			Buffer[Got] = 0;
			std::string Reply(Buffer);
			for (char& C : Reply) C = char(tolower(static_cast<unsigned char>(C)));
			const size_t At = Reply.find("\nlocation:");
			if (At == std::string::npos) continue;
			// The URL from the original (case kept).
			const std::string Original(Buffer);
			size_t Start = At + 10;
			while (Start < Original.size() && Original[Start] == ' ') ++Start;
			const size_t End = Original.find_first_of("\r\n", Start);
			const std::string Location = Original.substr(Start, End - Start);
			bool bKnown = false;
			for (const std::string& Each : Locations) bKnown = bKnown || Each == Location;
			if (!bKnown) Locations.push_back(Location);
		}
		closesocket(Socket);
		return Locations;
	}

	struct FUrl
	{
		std::wstring Host;
		INTERNET_PORT Port = 80;
		std::wstring Path;
	};

	bool ParseUrl(const std::string& Url, FUrl& Out)
	{
		URL_COMPONENTS Parts{};
		Parts.dwStructSize = sizeof(Parts);
		wchar_t Host[256] = {}, Path[1024] = {};
		Parts.lpszHostName = Host;
		Parts.dwHostNameLength = 255;
		Parts.lpszUrlPath = Path;
		Parts.dwUrlPathLength = 1023;
		const std::wstring Wide = Widen(Url);
		if (!WinHttpCrackUrl(Wide.c_str(), 0, 0, &Parts)) return false;
		Out.Host = Host;
		Out.Port = Parts.nPort;
		Out.Path = Path[0] ? Path : L"/";
		return true;
	}

	// One HTTP request to the router; Body empty means GET.
	bool Http(const std::string& Url, const std::string& SoapAction, const std::string& Body, std::string& Reply, DWORD& Status)
	{
		FUrl Parts;
		if (!ParseUrl(Url, Parts)) return false;
		HINTERNET Session = WinHttpOpen(L"Telescreen", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!Session) return false;
		WinHttpSetTimeouts(Session, 2000, 2000, 3000, 3000);
		bool bOk = false;
		HINTERNET Connection = WinHttpConnect(Session, Parts.Host.c_str(), Parts.Port, 0);
		HINTERNET Request = Connection ? WinHttpOpenRequest(Connection, Body.empty() ? L"GET" : L"POST", Parts.Path.c_str(), nullptr, WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
		if (Request)
		{
			std::wstring Headers;
			if (!Body.empty())
			{
				Headers = L"Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + Widen(SoapAction) + L"\"\r\n";
			}
			if (WinHttpSendRequest(Request, Headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : Headers.c_str(), DWORD(-1L),
					Body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(Body.data()), DWORD(Body.size()), DWORD(Body.size()), 0) &&
				WinHttpReceiveResponse(Request, nullptr))
			{
				DWORD Size = sizeof(Status);
				WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &Status, &Size,
					WINHTTP_NO_HEADER_INDEX);
				char Buffer[4096];
				DWORD Read = 0;
				while (WinHttpReadData(Request, Buffer, sizeof(Buffer), &Read) && Read > 0 && Reply.size() < (1u << 20)) Reply.append(Buffer, Read);
				bOk = true;
			}
			WinHttpCloseHandle(Request);
		}
		if (Connection) WinHttpCloseHandle(Connection);
		WinHttpCloseHandle(Session);
		return bOk;
	}

	// The address this PC uses to reach the router.
	std::string LocalIpFor(const std::string& Url)
	{
		FUrl Parts;
		if (!ParseUrl(Url, Parts)) return std::string();
		SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		sockaddr_in To{};
		To.sin_family = AF_INET;
		To.sin_port = htons(Parts.Port);
		std::string Result;
		if (InetPtonW(AF_INET, Parts.Host.c_str(), &To.sin_addr) == 1 && connect(Socket, reinterpret_cast<const sockaddr*>(&To), sizeof(To)) == 0)
		{
			sockaddr_in Local{};
			int Length = sizeof(Local);
			getsockname(Socket, reinterpret_cast<sockaddr*>(&Local), &Length);
			char Text[64] = {};
			inet_ntop(AF_INET, &Local.sin_addr, Text, sizeof(Text));
			Result = Text;
		}
		closesocket(Socket);
		return Result;
	}

	bool FindGateway(const std::string& Location, FGateway& Out)
	{
		std::string Description;
		DWORD Status = 0;
		if (!Http(Location, std::string(), std::string(), Description, Status) || Status != 200) return false;
		for (const char* Service : {"urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1",
				 "urn:schemas-upnp-org:service:WANPPPConnection:1"})
		{
			const size_t At = Description.find(std::string("<serviceType>") + Service + "</serviceType>");
			if (At == std::string::npos) continue;
			std::string Control = Between(Description, "<controlURL>", "</controlURL>", At);
			if (Control.empty()) continue;
			if (Control.rfind("http", 0) != 0)
			{
				// Relative to URLBase, or to the description's host.
				std::string Base = Between(Description, "<URLBase>", "</URLBase>");
				if (Base.empty())
				{
					const size_t Slash = Location.find('/', Location.find("//") + 2);
					Base = Slash == std::string::npos ? Location : Location.substr(0, Slash);
				}
				while (!Base.empty() && Base.back() == '/') Base.pop_back();
				Control = Base + (Control[0] == '/' ? "" : "/") + Control;
			}
			Out.ControlUrl = Control;
			Out.ServiceType = Service;
			Out.LocalIp = LocalIpFor(Location);
			return !Out.LocalIp.empty();
		}
		return false;
	}

	bool Soap(const FGateway& Gateway, const std::string& Action, const std::string& Arguments, std::string& Reply)
	{
		const std::string Body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
			"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" + Action + " xmlns:u=\"" + Gateway.ServiceType + "\">" + Arguments +
			"</u:" + Action + "></s:Body></s:Envelope>";
		DWORD Status = 0;
		return Http(Gateway.ControlUrl, Gateway.ServiceType + "#" + Action, Body, Reply, Status) && Status == 200;
	}
}

bool Upnp::MapUdpPort(uint16_t Port, std::string& ExternalIp, std::string& Error)
{
	std::lock_guard<std::mutex> Lock(GUpnpMutex);
	const std::vector<std::string> Locations = Discover();
	if (Locations.empty())
	{
		Error = "no UPnP router answered";
		return false;
	}
	for (const std::string& Location : Locations)
	{
		FGateway Gateway;
		if (!FindGateway(Location, Gateway)) continue;
		const std::string PortText = std::to_string(Port);
		std::string Reply;
		const std::string Mapping = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + PortText + "</NewExternalPort><NewProtocol>UDP</NewProtocol>"
			"<NewInternalPort>" + PortText + "</NewInternalPort><NewInternalClient>" + Gateway.LocalIp + "</NewInternalClient>"
			"<NewEnabled>1</NewEnabled><NewPortMappingDescription>Telescreen</NewPortMappingDescription><NewLeaseDuration>7200</NewLeaseDuration>";
		if (!Soap(Gateway, "AddPortMapping", Mapping, Reply))
		{
			Error = "the router refused the port mapping";
			LOG_WARN("upnp: AddPortMapping %u at %s refused", Port, Gateway.ControlUrl.c_str());
			continue;
		}
		std::string Address;
		if (Soap(Gateway, "GetExternalIPAddress", std::string(), Address)) ExternalIp = Between(Address, "<NewExternalIPAddress>", "</NewExternalIPAddress>");
		GMapped = Gateway;
		GMappedPort = Port;
		LOG_INFO("upnp: UDP %u mapped on %s (public %s)", Port, Gateway.ControlUrl.c_str(), ExternalIp.c_str());
		return true;
	}
	if (Error.empty()) Error = "no router with a usable UPnP service";
	return false;
}

void Upnp::Unmap()
{
	std::lock_guard<std::mutex> Lock(GUpnpMutex);
	if (GMappedPort == 0) return;
	std::string Reply;
	Soap(GMapped, "DeletePortMapping", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(GMappedPort) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>",
		Reply);
	LOG_INFO("upnp: UDP %u unmapped", GMappedPort);
	GMappedPort = 0;
}
