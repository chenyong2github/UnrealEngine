// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"

#if WITH_EDITOR
#include "WorldPartition/HLOD/HLODActorDescFactory.h"
#endif

#include "HLODSubsystem.generated.h"

class UWorldPartitionSubsystem;
class AWorldPartitionHLOD;
class UWorldPartitionRuntimeCell;

/**
 * UHLODSubsystem
 */
UCLASS()
class ENGINE_API UHLODSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UHLODSubsystem();
	virtual ~UHLODSubsystem();
		
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD);
	void UnregisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD);

	void TransitionToHLOD(const UWorldPartitionRuntimeCell* InCell);
	void TransitionFromHLOD(const UWorldPartitionRuntimeCell* InCell);
	
#if WITH_EDITOR
	FHLODActorDescFactory* GetActorDescFactory() const { return HLODActorDescFactory.Get(); }
#endif

private:
#if WITH_EDITOR
	void RegisterActorDescFactories(UWorldPartitionSubsystem* WorldPartitionSubsystem);
#endif

private:
#if WITH_EDITORONLY_DATA
	TUniquePtr<FHLODActorDescFactory> HLODActorDescFactory;
#endif

	// Mapping between an HLOD GUID & the loaded actor.
	TMap<FGuid, AWorldPartitionHLOD*> RegisteredHLODActors;

	// Cells that are waiting for their HLOD to load.
	TMultiMap<FGuid, FName> PendingTransitionsToHLOD;
};
