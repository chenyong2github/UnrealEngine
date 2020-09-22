// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

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
	inline const FSoftObjectPath& GetHLODLayer() const { return HLODLayer; }

protected:
	virtual void InitFrom(const AActor* InActor) override;
	virtual void Serialize(FArchive& Ar) override;

	TArray<FGuid> SubActors;
	FSoftObjectPath	HLODLayer;
#endif
};
