// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGMeshSelectorBase.h"


#include "PCGMeshSelectorByAttribute.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGMeshSelectorByAttribute : public UPCGMeshSelectorBase 
{
	GENERATED_BODY()

public:
	// ~Begin UObject interface
	void PostLoad() override;
	// ~End UObject interface

	virtual void SelectInstances_Implementation(
		UPARAM(ref) FPCGContext& Context,
		const UPCGStaticMeshSpawnerSettings* Settings,
		const UPCGSpatialData* InSpatialData,
		TArray<FPCGMeshInstanceList>& OutMeshInstances,
		UPCGPointData* OutPointData) const override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName AttributeName; 

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOverrideCollisionProfile = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "bOverrideCollisionProfile", EditConditionHides))
	FCollisionProfileName CollisionProfile = UCollisionProfile::NoCollision_ProfileName;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bOverrideMaterials_DEPRECATED = false;
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGMeshSelectorMaterialOverrideMode MaterialOverrideMode = EPCGMeshSelectorMaterialOverrideMode::NoOverride;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, DisplayName = "Static Material Overrides", Category = Settings, meta = (EditCondition = "MaterialOverrideMode==EPCGMeshSelectorMaterialOverrideMode::StaticOverride", EditConditionHides))
	TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, DisplayName = "By Attribute Material Overrides", Category = Settings, meta = (EditCondition = "MaterialOverrideMode==EPCGMeshSelectorMaterialOverrideMode::ByAttributeOverride", EditConditionHides))
	TArray<FName> MaterialOverrideAttributes;

	/** Distance at which instances begin to fade. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	float CullStartDistance = 0;
	
	/** Distance at which instances are culled. Use 0 to disable. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	float CullEndDistance = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	int32 WorldPositionOffsetDisableDistance = 0;
};
