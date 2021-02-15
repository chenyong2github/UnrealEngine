// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Blend Space 1D. Contains 1 axis blend 'space'
 *
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/BlendSpace.h"
#include "BlendSpace1D.generated.h"

UCLASS(config=Engine, hidecategories=Object, MinimalAPI, BlueprintType)
class UBlendSpace1D : public UBlendSpace
{
	GENERATED_UCLASS_BODY()

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bDisplayEditorVertically_DEPRECATED;
#endif

	/** Drive animation speed by blend input position **/
	UPROPERTY(EditAnywhere, Category = InputInterpolation)
	bool bScaleAnimation;

protected:
	//~ Begin UBlendSpace Interface
	virtual EBlendSpaceAxis GetAxisToScale() const override;
	//~ End UBlendSpace Interface
};
