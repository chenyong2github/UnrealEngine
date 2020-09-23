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

	//~ Begin USubsystem Interface.
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem Interface.

	void RegisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD);
	void UnregisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD);

	void OnCellShown(const UWorldPartitionRuntimeCell* InCell);
	void OnCellHidden(const UWorldPartitionRuntimeCell* InCell);
	
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

	// Cells that where shown before their HLOD was loaded.
	TMultiMap<FGuid, FName> PendingCellsShown;

#if WITH_EDITOR
	TMultiMap<FGuid, FGuid> PendingHLODAssignment;
#endif
};
