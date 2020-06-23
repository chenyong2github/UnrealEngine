// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkXRSourceSettings.generated.h"



USTRUCT(BlueprintType)
struct FLiveLinkXRSettings
{
    GENERATED_BODY()

public:

	FLiveLinkXRSettings():
		bTrackTrackers(true),
		bTrackControllers(false),
		bTrackHMDs(false),
		LocalUpdateRateInHz(60)
	{}
	
	/** Track all SteamVR tracker pucks */
	UPROPERTY(EditAnywhere, Category = "Data Source")
	bool bTrackTrackers;

	/** Track all controllers */
	UPROPERTY(EditAnywhere, Category = "Data Source")
	bool bTrackControllers;

	/** Track all HMDs */
	UPROPERTY(EditAnywhere, Category = "Data Source")
	bool bTrackHMDs;

	/** Update rate (in Hz) at which to read the tracking data for each device */
	UPROPERTY(EditAnywhere, Category = "Data Source", meta = (ClampMin = 1, ClampMax = 1000))
	uint32 LocalUpdateRateInHz;
};


UCLASS(Config=GameUserSettings)
class LIVELINKXR_API ULiveLinkXRSettingsObject : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, GlobalConfig, Category = "Data Source", Meta = (ShowOnlyInnerProperties))
    FLiveLinkXRSettings Settings;

    virtual bool IsEditorOnly() const override
    {
        return true;
    }
};


/**
 * 
 */
UCLASS()
class LIVELINKXR_API ULiveLinkXRSourceSettings : public ULiveLinkSourceSettings
{
	GENERATED_BODY()

public:
	ULiveLinkXRSourceSettings();
};
