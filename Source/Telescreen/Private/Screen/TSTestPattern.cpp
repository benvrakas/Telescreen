#include "Screen/TSTestPattern.h"
#include "Engine/Texture2D.h"
#include "Telescreen.h"
#include "TextureResource.h"

namespace
{
	constexpr int32 PatternWidth = 512;
	constexpr int32 PatternHeight = 256;
	constexpr int32 CornerSize = 48;

	struct FPatternColor
	{
		uint8 B, G, R, A;
	};

	TMap<int32, TWeakObjectPtr<UTexture2D>> GPatterns;

	FPatternColor PatternPixel(int32 X, int32 Y, int32 Variant)
	{
		// Corner markers show orientation: red top-left, green top-right, blue bottom-left, yellow bottom-right.
		const bool Left = X < CornerSize, Right = X >= PatternWidth - CornerSize;
		const bool Top = Y < CornerSize, Bottom = Y >= PatternHeight - CornerSize;
		if (Top && Left) return {0, 0, 255, 255};
		if (Top && Right) return {0, 255, 0, 255};
		if (Bottom && Left) return {255, 0, 0, 255};
		if (Bottom && Right) return {0, 255, 255, 255};

		if (X < 4 || Y < 4 || X >= PatternWidth - 4 || Y >= PatternHeight - 4) return {255, 255, 255, 255};
		if (X % 32 == 0 || Y % 32 == 0) return {90, 90, 90, 255};

		// An arrow pointing up in the centre column.
		const int32 Cx = PatternWidth / 2, Dy = Y - 70;
		if (Dy >= 0 && Dy < 50 && FMath::Abs(X - Cx) <= Dy / 2) return {255, 255, 255, 255};
		if (Y >= 120 && Y < 180 && FMath::Abs(X - Cx) <= 8) return {255, 255, 255, 255};

		static const FPatternColor Bars[3][4] = {
			{{0, 0, 192, 255}, {0, 192, 0, 255}, {192, 0, 0, 255}, {192, 192, 192, 255}},
			{{192, 0, 192, 255}, {0, 192, 192, 255}, {192, 192, 0, 255}, {64, 64, 64, 255}},
			{{40, 20, 10, 255}, {80, 40, 20, 255}, {120, 60, 30, 255}, {160, 80, 40, 255}},
		};
		const FPatternColor& Bar = Bars[Variant % 3][(X * 4) / PatternWidth];
		const float Shade = 0.4f + 0.6f * float(Y) / float(PatternHeight);
		return {uint8(Bar.B * Shade), uint8(Bar.G * Shade), uint8(Bar.R * Shade), 255};
	}
}

UTexture2D* TSTestPattern::Get(int32 Variant)
{
	if (TWeakObjectPtr<UTexture2D>* Existing = GPatterns.Find(Variant))
	{
		if (Existing->IsValid())
		{
			return Existing->Get();
		}
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(PatternWidth, PatternHeight, PF_B8G8R8A8, *FString::Printf(TEXT("TS_TestPattern_%d"), Variant));
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		UE_LOG(LogTelescreen, Error, TEXT("Could not create test pattern texture %d"), Variant);
		return nullptr;
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FPatternColor* Pixels = static_cast<FPatternColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Y = 0; Y < PatternHeight; ++Y)
	{
		for (int32 X = 0; X < PatternWidth; ++X)
		{
			Pixels[Y * PatternWidth + X] = PatternPixel(X, Y, Variant);
		}
	}
	Mip.BulkData.Unlock();

	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->AddToRoot();
	Texture->UpdateResource();
	GPatterns.Add(Variant, Texture);
	UE_LOG(LogTelescreen, Log, TEXT("Created test pattern texture %d (%dx%d)"), Variant, PatternWidth, PatternHeight);
	return Texture;
}
