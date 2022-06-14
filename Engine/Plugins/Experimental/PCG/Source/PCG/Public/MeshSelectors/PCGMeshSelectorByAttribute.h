// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGMeshSelectorBase.h"

#include "PCGMeshSelectorByAttribute.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGMeshSelectorByAttribute : public UPCGMeshSelectorBase 
{
	GENERATED_BODY()

public:
	virtual void SelectInstances_Implementation(
		UPARAM(ref) FPCGContext& Context,
		const UPCGStaticMeshSpawnerSettings* Settings,
		const UPCGSpatialData* InSpatialData,
		TMap<TSoftObjectPtr<UStaticMesh>, FPCGMeshInstanceList>& OutMeshInstances) const override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName AttributeName; 

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOverrideCollisionProfile = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FCollisionProfileName CollisionProfile;
};

