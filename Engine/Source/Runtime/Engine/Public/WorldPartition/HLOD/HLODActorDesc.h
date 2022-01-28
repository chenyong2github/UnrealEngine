// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"

#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/DataLayer/DataLayersID.h"
#include "WorldPartition/HLOD/HLODSubActor.h"

class UHLODLayer;

/**
 * ActorDesc for AWorldPartitionHLOD.
 */
class ENGINE_API FHLODActorDesc : public FWorldPartitionActorDesc
{
#if WITH_EDITOR
	friend class FHLODActorDescFactory;

public:
	inline const TArray<FHLODSubActorDesc>& GetSubActors() const { return HLODSubActors; }
	inline uint64 GetCellHash() const { return CellHash; }

	static uint64 ComputeCellHash(const FString HLODLayerName, uint64 GridIndexX, uint64 GridIndexY, uint64 GridIndexZ, FDataLayersID DataLayersID);

	virtual bool ShouldBeLoadedByEditorCells() const { return false; }

protected:
	virtual void Init(const AActor* InActor) override;
	virtual bool Equals(const FWorldPartitionActorDesc* Other) const override;
	virtual void Serialize(FArchive& Ar) override;

	TArray<FHLODSubActorDesc> HLODSubActors;

	uint64 CellHash = 0;
#endif
};
