// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGPoint.h"
#include "Metadata/PCGMetadata.h"

#include "Engine/CollisionProfile.h"

#include "PCGMeshSelectorBase.generated.h"

class UPCGPointData;
class UPCGSpatialData;
class UStaticMesh;
struct FPCGContext;

class UPCGStaticMeshSpawnerSettings;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FPCGMeshInstanceList
{
	GENERATED_BODY()

	FPCGMeshInstanceList() = default;

	FPCGMeshInstanceList(const TSoftObjectPtr<UStaticMesh>& InMesh, bool bInOverrideCollisionProfile, const FCollisionProfileName& InCollisionProfile, bool bInOverrideMaterials, const TArray<TSoftObjectPtr<UMaterialInterface>>& InMaterialOverrides, const float InCullStartDistance, const float InCullEndDistance, const int32 InWorldPositionOffsetDisableDistance, const bool bInIsLocalToWorldDeterminantNegative)
		: Mesh(InMesh), bOverrideCollisionProfile(bInOverrideCollisionProfile), CollisionProfile(InCollisionProfile), bOverrideMaterials(bInOverrideMaterials), MaterialOverrides(InMaterialOverrides), CullStartDistance(InCullStartDistance), CullEndDistance(InCullEndDistance), WorldPositionOffsetDisableDistance(InWorldPositionOffsetDisableDistance), bIsLocalToWorldDeterminantNegative(bInIsLocalToWorldDeterminantNegative)
	{}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOverrideCollisionProfile = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FCollisionProfileName CollisionProfile = UCollisionProfile::NoCollision_ProfileName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOverrideMaterials = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
    TArray<FPCGPoint> Instances;

	/** Distance at which instances begin to fade. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	float CullStartDistance = 0;
	
	/** Distance at which instances are culled. Use 0 to disable. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	float CullEndDistance = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	int32 WorldPositionOffsetDisableDistance = 0;

	/** Whether the culling should be reversed or not (needed to support negative scales) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bIsLocalToWorldDeterminantNegative = false;
};

UENUM()
enum class EPCGMeshSelectorMaterialOverrideMode : uint8
{
	NoOverride UMETA(Tooltip = "Does not apply any material overrides to the spawned mesh(es)"),
	StaticOverride UMETA(Tooltip = "Applies the material overrides provided in the Static Material Overrides array"),
	ByAttributeOverride UMETA(Tooltip = "Applies the materials overrides using the point data attribute(s) specified in the By Attribute Material Overrides array")
};

/** Struct used to efficiently gather overrides and cache them during instance packing */
struct FPCGMeshMaterialOverrideHelper
{
	// Use this constructor when you have a 1:1 mapping between attributes or static overrides
	FPCGMeshMaterialOverrideHelper(
		FPCGContext& InContext,
		EPCGMeshSelectorMaterialOverrideMode InMaterialOverrideMode,
		const TArray<TSoftObjectPtr<UMaterialInterface>>& InStaticMaterialOverrides,
		const TArray<FName>& InMaterialOverrideAttributeNames,
		const UPCGMetadata* InMetadata);

	// Use this constructor when you have common attribute usage or separate static overrides
	FPCGMeshMaterialOverrideHelper(
		FPCGContext& InContext,
		bool bInByAttributeOverride,
		const TArray<FName>& InMaterialOverrideAttributeNames,
		const UPCGMetadata* InMetadata);

	bool IsValid() const { return bIsValid; }
	bool OverridesMaterials() const { return MaterialOverrideMode != EPCGMeshSelectorMaterialOverrideMode::NoOverride; }
	const TArray<TSoftObjectPtr<UMaterialInterface>>& GetMaterialOverrides(PCGMetadataEntryKey EntryKey);

	// Cached data
	TArray<const FPCGMetadataAttribute<FString>*> MaterialAttributes;
	TArray<TMap<PCGMetadataValueKey, TSoftObjectPtr<UMaterialInterface>>> ValueKeyToOverrideMaterials;
	TArray<TSoftObjectPtr<UMaterialInterface>> WorkingMaterialOverrides;
	TArray<TSoftObjectPtr<UMaterialInterface>> EmptyArray;

	// Data needed to perform operations
	bool bIsValid = false;
	EPCGMeshSelectorMaterialOverrideMode MaterialOverrideMode = EPCGMeshSelectorMaterialOverrideMode::NoOverride;

	const TArray<TSoftObjectPtr<UMaterialInterface>>& StaticMaterialOverrides;
	const TArray<FName>& MaterialOverrideAttributeNames;
	const UPCGMetadata* Metadata = nullptr;

private:
	void Initialize(FPCGContext& InContext);
};

UCLASS(Abstract, BlueprintType, Blueprintable, ClassGroup = (Procedural))
class PCG_API UPCGMeshSelectorBase : public UObject 
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = MeshSelection)
	void SelectInstances(
		FPCGContext& Context,
		const UPCGStaticMeshSpawnerSettings* Settings,
		const UPCGPointData* InPointData,
		TArray<FPCGMeshInstanceList>& OutMeshInstances,
		UPCGPointData* OutPointData) const;

	virtual void SelectInstances_Implementation(
		FPCGContext& Context,
		const UPCGStaticMeshSpawnerSettings* Settings,
		const UPCGPointData* InPointData,
		TArray<FPCGMeshInstanceList>& OutMeshInstances,
		UPCGPointData* OutPointData) const PURE_VIRTUAL(UPCGMeshSelectorBase::SelectInstances_Implementation);

	/** Searches OutInstanceLists for an InstanceList matching the given parameters. If nothing is found, creates a new InstanceList and adds to OutInstanceLists. Returns index of the matching instance list. */
	UFUNCTION(BlueprintCallable, Category = MeshSelection)
	static int32 FindOrAddInstanceList(
		TArray<FPCGMeshInstanceList>& OutInstanceLists,
		const TSoftObjectPtr<UStaticMesh>& Mesh,
		bool bOverrideCollisionProfile,
		const FCollisionProfileName& CollisionProfile,
		bool bOverrideMaterials,
		const TArray<TSoftObjectPtr<UMaterialInterface>>& MaterialOverrides,
		const float InCullStartDistance,
		const float InCullEndDistance,
		const int32 InWorldPositionOffsetDisableDistance,
		const bool bInIsLocalToWorldDeterminantNegative);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "Data/PCGPointData.h"
#include "PCGElement.h"
#endif
