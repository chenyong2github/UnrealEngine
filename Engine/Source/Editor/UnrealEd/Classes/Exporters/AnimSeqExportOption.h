// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Import data and options used when export an animation sequence
 */

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AnimSeqExportOption.generated.h"

UCLASS(MinimalAPI)
class UAnimSeqExportOption : public UObject
{
	GENERATED_UCLASS_BODY()
public:

	/** If enabled, export the transforms from the animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Export)
	bool bExportTransforms = true;

	/** If enabled, export the curves from the animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Export)
	bool bExportCurves = true;

	/** If enabled we record in World Space otherwise we record from 0,0,0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Export)
	bool bRecordInWorldSpace = false;

	/** If true we evaluate all other skeletal mesh components under the same actor, this may be needed for example, to get physics to get baked*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Export);	
	bool bEvaluateAllSkeletalMeshComponents = false;

	/** Number of Display Rate frames to evaluate before doing the export. Use it if there is some post anim BP effects you want to warm up*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Export);
	FFrameNumber WarmUpFrames;

	void ResetToDefault()
	{
		bExportTransforms = true;
		bExportCurves = true;
		bRecordInWorldSpace = false;
		bEvaluateAllSkeletalMeshComponents = false;
		WarmUpFrames = 0;
	}
};
