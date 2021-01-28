// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"

#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/DataLayer/DataLayersID.h"

class UHLODLayer;

/**
 * ActorDesc for AWorldPartitionHLOD.
 */
class ENGINE_API FHLODActorDesc : public FWorldPartitionActorDesc
{
#if WITH_EDITOR
	friend class FHLODActorDescFactory;

public:
	inline const TArray<FGuid>& GetSubActors() const { return SubActors; }
	inline uint64 GetCellHash() const { return CellHash; }

	static uint64 ComputeCellHash(const FString HLODLayerName, uint64 GridIndexX, uint64 GridIndexY, uint64 GridIndexZ, FDataLayersID DataLayersID);

protected:
	virtual void Init(const AActor* InActor) override;
	virtual void Serialize(FArchive& Ar) override;

	TArray<FGuid> SubActors;

	uint64 CellHash = 0;
#endif
};
