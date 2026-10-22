#pragma once

#include "Common.h"

// Security attributes that allow only the current Windows user (and SYSTEM) to open an object; used for the pipe and
// the shared memory sections. Falls back to the default security if the descriptor can't be built.
class FUserOnlySecurity
{
public:
	FUserOnlySecurity();
	~FUserOnlySecurity();

	SECURITY_ATTRIBUTES* Get() { return Descriptor ? &Attributes : nullptr; }

private:
	PSECURITY_DESCRIPTOR Descriptor = nullptr;
	SECURITY_ATTRIBUTES Attributes{};
};
