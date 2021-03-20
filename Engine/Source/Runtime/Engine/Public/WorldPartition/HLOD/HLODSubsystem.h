// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"

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
	
private:

private:
	struct FCellHLODMapping
	{
		bool						bIsCellVisible;
		TSet<AWorldPartitionHLOD*>	LoadedHLODs;

		FCellHLODMapping() : bIsCellVisible(false)
		{
		}
	};

	TMap<FName, FCellHLODMapping> CellsHLODMapping;
};
