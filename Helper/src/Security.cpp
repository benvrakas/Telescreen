#include "Security.h"

#include <sddl.h>

FUserOnlySecurity::FUserOnlySecurity()
{
	HANDLE Token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
	{
		LOG_WARN("OpenProcessToken failed (%lu); using default security", GetLastError());
		return;
	}
	DWORD Size = 0;
	GetTokenInformation(Token, TokenUser, nullptr, 0, &Size);
	std::vector<uint8_t> Buffer(Size);
	LPWSTR SidString = nullptr;
	if (GetTokenInformation(Token, TokenUser, Buffer.data(), Size, &Size) &&
		ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(Buffer.data())->User.Sid, &SidString))
	{
		const std::wstring Sddl = L"D:P(A;;GA;;;" + std::wstring(SidString) + L")(A;;GA;;;SY)";
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl.c_str(), SDDL_REVISION_1, &Descriptor, nullptr))
		{
			LOG_WARN("could not build the user-only security descriptor (%lu); using default security", GetLastError());
			Descriptor = nullptr;
		}
		LocalFree(SidString);
	}
	CloseHandle(Token);
	Attributes.nLength = sizeof(Attributes);
	Attributes.lpSecurityDescriptor = Descriptor;
	Attributes.bInheritHandle = FALSE;
}

FUserOnlySecurity::~FUserOnlySecurity()
{
	if (Descriptor) LocalFree(Descriptor);
}
