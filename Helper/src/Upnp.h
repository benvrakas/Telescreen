#pragma once

#include "Common.h"

#include <string>

// Opens a UDP port on the home router with UPnP (Internet Gateway Device: WANIPConnection or WANPPPConnection), so
// players outside can reach the direct-stream port. Routers with UPnP off simply don't answer.
namespace Upnp
{
	// Maps external Port to this PC's Port; ExternalIp is the router's public address. Takes up to a few seconds.
	bool MapUdpPort(uint16_t Port, std::string& ExternalIp, std::string& Error);

	// Removes the mapping MapUdpPort made, if any.
	void Unmap();
}
