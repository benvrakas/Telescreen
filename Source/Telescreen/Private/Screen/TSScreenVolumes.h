#pragma once

#include "CoreMinimal.h"

class AActor;

// Each player's own volume for each screen ("My volume", 0-2, default 1), kept on their PC in
// Saved/Telescreen/ScreenVolumes.json. A screen is known by its name and position, which last across saves.
namespace TSScreenVolumes
{
	float Get(const AActor* Screen);

	// Sets the volume now; bSave also writes the file (when the slider is let go).
	void Set(const AActor* Screen, float Volume, bool bSave);
}
