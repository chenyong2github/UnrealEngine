// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MagicLeapSettings.generated.h"

/**
* Implements the settings for the Magic Leap SDK setup.
*/
UCLASS(config=Engine, defaultconfig)
class MAGICLEAP_API UMagicLeapSettings : public UObject
{
public:
	GENERATED_BODY()

	// Enables 'Zero Iteration mode'. Note: Vulkan rendering will be used by default. Set bUseVulkan to false to use OpenGL instead.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "General", Meta = (DisplayName = "Enable Zero Iteration", ConfigRestartRequired = true))
	bool bEnableZI;

	// Use the editor in Vulkan. If False, OpenGL is used with ZI.
	UPROPERTY(GlobalConfig, Meta = (ConfigRestartRequired = true))
	bool bUseVulkanForZI;

	// Use the MagicLeapAudio mixer module when using ZI. This will play audio via the ML device. Otherwise, the audio is played on the host machine itself.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "General", Meta = (DisplayName = "Use MLAudio for Zero Iteration (host audio otherwise)", ConfigRestartRequired = true))
	bool bUseMLAudioForZI;
};
