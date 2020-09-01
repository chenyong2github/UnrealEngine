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
	bool bExportTransforms;

	/** If enabled, export the curves from the animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay,  Category = Export)
	bool bExportCurves;

	/** If enabled we record in World Space otherwise we record from 0,0,0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Export)
	bool bRecordInWorldSpace;

	void ResetToDefault()
	{
		bExportTransforms = true;
		bExportCurves = true;
		bRecordInWorldSpace = false;
	}
};
