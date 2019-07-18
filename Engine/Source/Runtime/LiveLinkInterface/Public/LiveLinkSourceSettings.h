// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LiveLinkSourceFactory.h"
#include "Misc/FrameRate.h"
#include "Templates/SubclassOf.h"

#include "LiveLinkSourceSettings.generated.h"

UENUM()
enum class ELiveLinkSourceMode : uint8
{
	//The source will be run in default mode.
	//This mode will not attempt any type of interpolation, time synchronization,
	//or other processing.
	Default,				

	//The source will be run in interpolated mode.
	//This mode will use FLiveLinkInterpolationSettings and is most useful
	//when smooth animation is desired.
	Interpolated,		

	//The source will be run in time synchronized mode.
	//This mode will use FLiveLinkTimeSynchronizationSettings and is most useful
	//when sources need to be synchronized with multiple other external inputs
	//(such as video or other time synchronized sources).
	//Don't use if the engine isn't setup with a Timecode provider.
	TimeSynchronized,	
};

USTRUCT()
struct FLiveLinkTimeSynchronizationSettings
{
	GENERATED_BODY()

	FLiveLinkTimeSynchronizationSettings() = default;

	// The frame rate of the source.
	// This should be the frame rate the source is "stamped" at, not necessarily the frame rate the source is sending.
	// The source should supply this whenever possible.
	UPROPERTY(EditAnywhere, Category = Settings)
	FFrameRate FrameRate = {60, 1};

	// When evaluating: how far back from current timecode should we read the buffer (in frame number)
	UPROPERTY(EditAnywhere, Category = Settings)
	int32 FrameDelay = 0;
};
 
USTRUCT()
struct FLiveLinkInterpolationSettings
{
	GENERATED_BODY()

	FLiveLinkInterpolationSettings() = default;

#if WITH_EDITORONLY_DATA
	//UE_DEPRECATED(4.21, "Please use ULiveLinkSourceSettings::Mode to specify how the source will behave.")
	UPROPERTY()
	bool bUseInterpolation_DEPRECATED = false;
#endif

	// When evaluating: how far back from current time should we read the buffer (in seconds)
	UPROPERTY(EditAnywhere, Category = Settings)
	float InterpolationOffset = 0.5f;
};

// Base class for live link source settings (can be replaced by sources themselves) 
UCLASS()
class LIVELINKINTERFACE_API ULiveLinkSourceSettings : public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings")
	ELiveLinkSourceMode Mode = ELiveLinkSourceMode::Default;

	// Only used when Mode is set to Interpolated.
	UPROPERTY(EditAnywhere, Category = "Interpolation Settings")
	FLiveLinkInterpolationSettings InterpolationSettings;

	// Only used when Mode is set to TimeSynchronized.
	UPROPERTY(EditAnywhere, Category = "Time Synchronization Settings")
	FLiveLinkTimeSynchronizationSettings TimeSynchronizationSettings;

	UPROPERTY(EditAnywhere, Category = "Settings", AdvancedDisplay)
	FString ConnectionString;

	UPROPERTY(VisibleAnywhere, Category = "Settings", AdvancedDisplay)
	TSubclassOf<ULiveLinkSourceFactory> Factory;

	virtual void Serialize(FArchive& Ar) override;
};
