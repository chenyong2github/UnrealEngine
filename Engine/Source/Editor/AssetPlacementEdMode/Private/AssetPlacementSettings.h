// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "FoliageType.h"

#include "AssetPlacementSettings.generated.h"

class IAssetFactoryInterface;

USTRUCT()
struct FPaletteItem
{
	GENERATED_BODY()

	UPROPERTY()
	FAssetData AssetData;

	UPROPERTY()
	TScriptInterface<IAssetFactoryInterface> FactoryOverride;
};

UCLASS(config = EditorPerProjectUserSettings)
class UAssetPlacementSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Axis Alignment", meta = (InlineEditConditionToggle))
	bool bAlignToNormal = true;
	
	UPROPERTY(config, EditAnywhere, Category = "Axis Alignment", meta = (DisplayName = "Align To Normal", EditCondition = "bAlignToNormal"))
	TEnumAsByte<EAxis::Type> AxisToAlignWithNormal = EAxis::Type::Z;

	UPROPERTY(config, EditAnywhere, Category = "Axis Alignment")
	bool bInvertNormalAxis = false;
	
	UPROPERTY(config, EditAnywhere, Category = "Rotation", meta = (InlineEditConditionToggle))
	bool bUseRandomXRotation = false;

	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Rotation", meta = (EditCondition = "bUseRandomXRotation", UIMin = "0.0", UIMax = "360.0"))
	FFloatInterval RandomRotationX = FFloatInterval(0.0f, 360.0f);
	
	UPROPERTY(config, EditAnywhere, Category = "Rotation", meta = (InlineEditConditionToggle))
	bool bUseRandomYRotation = false;

	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Rotation", meta = (EditCondition = "bUseRandomYRotation", UIMin = "0.0", UIMax = "360.0"))
	FFloatInterval RandomRotationY = FFloatInterval(0.0f, 360.0f);

	UPROPERTY(config, EditAnywhere, Category = "Rotation", meta = (InlineEditConditionToggle))
	bool bUseRandomZRotation = true;

	UPROPERTY(config, EditAnywhere, Category = "Rotation", meta = (EditCondition = "bUseRandomZRotation", UIMin = "0.0", UIMax = "360.0"))
	FFloatInterval RandomRotationZ = FFloatInterval(0.0f, 360.0f);

	UPROPERTY(config, EditAnywhere, Category = "Scale", meta = (InlineEditConditionToggle))
	bool bUseRandomScale = true;

	UPROPERTY(config, EditAnywhere, Category = "Scale", meta = (DisplayName="Use Random Scale", EditCondition = "bAllowRandomScale"))
	EFoliageScaling ScalingType = EFoliageScaling::Uniform;
	
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Scale", meta = (UIMin = ".01"))
	FFloatInterval ScaleRangeUniform = FFloatInterval(.8f, 1.0f);
	
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Scale")
	bool bAllowNegativeUniformScale = false;
	
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Scale", meta = (UIMin = ".01"))
	FFloatInterval ScaleRangeX = FFloatInterval(.8f, 1.0f);
	
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Scale")
	bool bAllowNegativeXScale = false;

	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Scale", meta = (UIMin = ".01"))
	FFloatInterval ScaleRangeY = FFloatInterval(.8f, 1.0f);
	
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Scale")
	bool bAllowNegativeYScale = false;

	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Scale", meta = (UIMin = ".01"))
	FFloatInterval FreeScaleRangeZ = FFloatInterval(.8f, 1.0f);
	
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Scale")
	bool bAllowNegativeZScale = false;

	UPROPERTY(config, EditAnywhere, Category = "Place on Filters")
	bool bLandscape = true;

	UPROPERTY(config, EditAnywhere, Category = "Place on Filters")
	bool bStaticMeshes = true;

	UPROPERTY(config, EditAnywhere, Category = "Place on Filters")
	bool bBSP = true;

	UPROPERTY(config, EditAnywhere, Category = "Place on Filters")
	bool bFoliage = false;

	UPROPERTY(config, EditAnywhere, Category = "Place on Filters")
	bool bTranslucent = false;

	// todo: asset data does not serialize out correctly
	// maybe save soft object pointers, and convert in the UI to asset data?
	//UPROPERTY(config)
	TArray<FPaletteItem> PaletteItems;

	virtual bool CanEditChange(const FProperty* InProperty) const override;
};
