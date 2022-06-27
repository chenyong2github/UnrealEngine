// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "WaveTableSettings.generated.h"


UENUM(BlueprintType)
enum class EWaveTableResolution : uint8
{
	None		= 0 UMETA(DisplayName = "None"),
	Res_8		= 3 UMETA(DisplayName = "8"),
	Res_16		= 4 UMETA(DisplayName = "16"),
	Res_32		= 5 UMETA(DisplayName = "32"),
	Res_64		= 6 UMETA(DisplayName = "64"),
	Res_128		= 7 UMETA(DisplayName = "128"),
	Res_256		= 8 UMETA(DisplayName = "256"),
	Res_512		= 9 UMETA(DisplayName = "512"),
	Res_1024	= 10 UMETA(DisplayName = "1024"),
	Res_2048	= 11 UMETA(DisplayName = "2048"),
	Res_4096	= 12 UMETA(DisplayName = "4096"),

	MAX = Res_4096 UMETA(Hidden)
};


USTRUCT()
struct WAVETABLE_API FWaveTableSettings
{
	GENERATED_USTRUCT_BODY()

	// File to import
	UPROPERTY(EditAnywhere, Category = Options)
	FFilePath FilePath;

	// Index of channel in file to build WaveTable from (wraps if channel is greater than number in file)
	UPROPERTY(EditAnywhere, Category = Options, meta = (ClampMin = "0.0"))
	int32 ChannelIndex = 0;

	// Percent to phase shift of table
	UPROPERTY(EditAnywhere, Category = Options, meta = (ClampMin = "0.0", ClampMax = "1.0", SliderExponent = 0.5))
	float Phase = 0.0f;

	// Percent to remove from beginning of sampled WaveTable.
	UPROPERTY(EditAnywhere, Category = Options, meta = (ClampMin = "0.0", ClampMax = "1.0", SliderExponent = 0.5))
	float Top = 0.0f;

	// Percent to remove from end of sampled WaveTable.
	UPROPERTY(EditAnywhere, Category = Options, meta = (ClampMin = "0.0", ClampMax = "1.0", SliderExponent = 0.5))
	float Tail = 0.0f;

	// Percent to fade in over.
	UPROPERTY(EditAnywhere, Category = Options, meta = (ClampMin = "0.0", ClampMax = "1.0", SliderExponent = 0.5))
	float FadeIn  = 0.0f;

	// Percent to fade out over.
	UPROPERTY(EditAnywhere, Category = Options, meta = (ClampMin = "0.0", ClampMax = "1.0", SliderExponent = 0.5))
	float FadeOut = 0.0f;

	// Whether or not to normalize the WaveTable.
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayAfter = "FilePath"))
	bool bNormalize = true;

	// Whether or not to remove offset from original file
	// (analogous to "DC offset" in circuit theory).
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayAfter = "bNormalize"))
	bool bRemoveOffset = false;

	// SourcePCM Data
	UPROPERTY()
	TArray<float> SourcePCMData;
};

UENUM(BlueprintType)
enum class EWaveTableCurve : uint8
{
	Linear		UMETA(DisplayName = "Linear (Ramp In)"),
	Linear_Inv	UMETA(DisplayName = "Linear (Ramp Out)"),
	Exp			UMETA(DisplayName = "Exponential"),
	Exp_Inverse UMETA(DisplayName = "Exponential (Inverse)"),
	Log			UMETA(DisplayName = "Log"),

	Sin			UMETA(DisplayName = "Sin (90 deg)"),
	Sin_Full	UMETA(DisplayName = "Sin (360 deg)"),
	SCurve		UMETA(DisplayName = "Sin (+/- 90 deg)"),

	// Reference a shared curve asset
	Shared		UMETA(DisplayName = "Shared"),

	// Design a custom curve unique to the owning transform
	Custom		UMETA(DisplayName = "Custom"),

	// Generate WaveTable from audio file
	File		UMETA(DisplayName = "File"),

	Count		UMETA(Hidden),
};

namespace WaveTable
{
	// Converts WaveTableResolution to integer resolution value
	WAVETABLE_API int32 ResolutionToInt32(EWaveTableResolution InWaveTableResolution);

	// Converts WaveTableResolution to integer resolution value.  If WaveTableResolution is set to none, uses
	// default value associated with provided curve.
	WAVETABLE_API int32 ResolutionToInt32(EWaveTableResolution InWaveTableResolution, EWaveTableCurve InCurve);
} // namespace WaveTable