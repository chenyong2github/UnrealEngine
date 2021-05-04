// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"

#include "HLODSubsystem.generated.h"

class UWorldPartition;
class UWorldPartitionRuntimeCell;
class AWorldPartitionHLOD;

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

	static bool IsHLODEnabled();
	
private:
	void OnWorldPartitionRegistered(UWorldPartition* InWorldPartition);
	void OnWorldPartitionUnregistered(UWorldPartition* InWorldPartition);

	/** Console command used to turn on/off loading & rendering of world partition HLODs */
	static class FAutoConsoleCommand EnableHLODCommand;

private:

	static bool WorldPartitionHLODEnabled;

	struct FCellHLODMapping
	{
		bool						bIsCellVisible;
		TSet<AWorldPartitionHLOD*>	LoadedHLODs;
		

		FCellHLODMapping() : bIsCellVisible(false)
		{
		}
	};

	TMap<TSoftObjectPtr<UWorldPartitionRuntimeCell>, FCellHLODMapping> CellsHLODMapping;
};
