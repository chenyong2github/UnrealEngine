// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Retargeter/IKRetargetSettings.h"

#include "IKRetargetProfile.generated.h"

class UIKRetargeter;
class URetargetChainSettings;

USTRUCT(BlueprintType)
struct FRetargetProfile
{
	GENERATED_BODY()
	
public:
	// Override the TARGET Retarget Pose to use when this profile is active.
	// The pose must be present in the Retarget Asset and is not applied unless bApplyTargetRetargetPose is true.
	UPROPERTY(BlueprintReadWrite, Category=RetargetPoses)
	FName TargetRetargetPoseName;

	// If true, the TARGET Retarget Pose specified in this profile will be applied to the Retargeter (when plugged into the Retargeter).
	UPROPERTY(BlueprintReadWrite, Category=RetargetPoses)
	bool bApplyTargetRetargetPose = false;

	// Override the SOURCE Retarget Pose to use when this profile is active.
	// The pose must be present in the Retarget Asset and is not applied unless bApplySourceRetargetPose is true.
	UPROPERTY(BlueprintReadWrite, Category=RetargetPoses)
	FName SourceRetargetPoseName;
	
	// If true, the Source Retarget Pose specified in this profile will be applied to the Retargeter (when plugged into the Retargeter).
	UPROPERTY(BlueprintReadWrite, Category=RetargetPoses)
	bool bApplySourceRetargetPose = false;

	// A (potentially sparse) set of setting overrides for the target chains (only applied when bApplyChainSettings is true).
	UPROPERTY(BlueprintReadWrite, Category=ChainSettings)
	TMap<FName, FTargetChainSettings> ChainSettings;

	// If true, the Chain Settings stored in this profile will be applied to the Retargeter (when plugged into the Retargeter).
	UPROPERTY(BlueprintReadWrite, Category=ChainSettings)
	bool bApplyChainSettings = true;

	// Retarget settings to control behavior of the retarget root motion (not applied unless bApplyRootSettings is true)
	UPROPERTY(BlueprintReadWrite, Category=RootSettings)
	FTargetRootSettings RootSettings;

	// If true, the root settings stored in this profile will be applied to the Retargeter (when plugged into the Retargeter).
	UPROPERTY(BlueprintReadWrite, Category=RootSettings)
	bool bApplyRootSettings = false;
};
