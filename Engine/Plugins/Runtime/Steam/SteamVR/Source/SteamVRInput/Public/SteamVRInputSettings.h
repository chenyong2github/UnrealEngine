// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "SteamVRInputSettings.generated.h"

/**
* Implements the settings for SteamVR input.
*/
UCLASS(config = Editor, defaultconfig)
class STEAMVRINPUT_API USteamVRInputSettings : public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "General", Meta = (DisplayName = "Enable SteamVR Input developer mode", Tooltip = "Disables automatic manifest generation and enables toolbar icon", ConfigRestartRequired = true))
	bool bDeveloperMode = false;
};
