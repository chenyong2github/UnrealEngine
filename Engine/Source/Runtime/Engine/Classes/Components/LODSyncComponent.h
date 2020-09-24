// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "Framework/Commands/InputChord.h"
#include "LODSyncComponent.generated.h"

USTRUCT(BlueprintType)
struct FLODMappingData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FLODMappingData)
	TArray<int32> Mapping;

	UPROPERTY(transient)
	TArray<int32> InverseMapping;
};
/**
 * Implement an Actor component for LOD Sync of different components
 *
 * This is a component that allows multiple different components to sync together of their LOD
 *
 * This allows to find the highest LOD of all the parts, and sync to that LOD
 */
UCLASS(Blueprintable, ClassGroup = Component, BlueprintType, meta = (BlueprintSpawnableComponent))
class ENGINE_API ULODSyncComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

	// if -1, it's default and it will calculate the max number of LODs from all sub components
	// if not, it is a number of LODs (not the max index of LODs)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LOD)
	int32 NumLODs = -1;

	// if -1, it's automatically switching
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LOD)
	int32 ForcedLOD = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LOD)
	TArray<FName> ComponentsToSync;

	// by default, the mapping will be one to one
// but if you want custom, add here. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LOD)
	TMap<FName, FLODMappingData> CustomLODMapping;

private:
	UPROPERTY(transient)
	int32 CurrentLOD = 0;

	// num of LODs
	UPROPERTY(transient)
	int32 CurrentNumLODs = 0;

	UPROPERTY(transient)
	TArray<UPrimitiveComponent*> SubComponents;

	// BEGIN AActorComponent interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// END AActorComponent interface

public: 
	void RefreshSyncComponents();

private:
	int32 GetCustomMappingLOD(const FName& ComponentName, int32 CurrentWorkingLOD) const;
	int32 GetSyncMappingLOD(const FName& ComponentName, int32 CurrentSourceLOD) const;
	void InitializeSyncComponents();
	void UninitializeSyncComponents();
};

