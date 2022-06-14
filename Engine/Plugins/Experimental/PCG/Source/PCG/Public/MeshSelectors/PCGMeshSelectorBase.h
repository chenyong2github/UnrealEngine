// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"
#include "PCGPoint.h"
#include "Data/PCGPointData.h"

#include "Engine/CollisionProfile.h"

#include "PCGMeshSelectorBase.generated.h"

class UPCGStaticMeshSpawnerSettings;

USTRUCT(BlueprintType)
struct FPCGMeshInstanceList
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
    TArray<FTransform> Instances;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOverrideCollisionProfile = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FCollisionProfileName CollisionProfile;
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
		const UPCGSpatialData* InSpatialData,
		TMap<TSoftObjectPtr<UStaticMesh>, FPCGMeshInstanceList>& OutMeshInstances) const;

	virtual void SelectInstances_Implementation(
		FPCGContext& Context,
		const UPCGStaticMeshSpawnerSettings* Settings,
		const UPCGSpatialData* InSpatialData,
		TMap<TSoftObjectPtr<UStaticMesh>, FPCGMeshInstanceList>& OutMeshInstances) const;
};
