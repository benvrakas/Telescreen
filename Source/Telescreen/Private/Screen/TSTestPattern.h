#pragma once

#include "CoreMinimal.h"

class UTexture2D;

namespace TSTestPattern
{
	// Returns the shared test pattern texture for Variant (created on first use, kept for the session).
	UTexture2D* Get(int32 Variant);
}
