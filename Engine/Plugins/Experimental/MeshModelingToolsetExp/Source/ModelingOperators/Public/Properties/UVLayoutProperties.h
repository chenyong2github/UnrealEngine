// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveTool.h"

#include "UVLayoutProperties.generated.h"

UENUM()
enum class EUVLayoutType
{
	Transform,
	Stack,
	Repack
};


/**
 * Standard properties
 */
UCLASS()
class MODELINGOPERATORS_API UUVLayoutProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Type of transformation to apply to input UV islands */
	UPROPERTY(EditAnywhere, Category = UVLayout)
	EUVLayoutType LayoutType = EUVLayoutType::Repack;

	/** Expected resolution of output textures; controls spacing left between charts */
	UPROPERTY(EditAnywhere, Category = UVLayout, meta = (UIMin = "64", UIMax = "2048", ClampMin = "2", ClampMax = "4096"))
	int TextureResolution = 1024;

	/** Apply this uniform scaling to the UVs after any layout recalculation */
	UPROPERTY(EditAnywhere, Category = UVLayout, meta = (UIMin = "0.1", UIMax = "5.0", ClampMin = "0.0001", ClampMax = "10000") )
	float UVScaleFactor = 1;

	/** Apply this 2D translation to the UVs after any layout recalculation, and after scaling */
	UPROPERTY(EditAnywhere, Category = UVLayout)
	FVector2D UVTranslate = FVector2D(0,0);

	/** Allow the packer to flip the orientation of UV islands if it save space. May cause problems for downstream operations, not recommended. */
	UPROPERTY(EditAnywhere, Category = UVLayout, AdvancedDisplay)
	bool bAllowFlips = false;

};