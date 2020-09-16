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
	virtual bool Init(const AActor* InActor) override;

	virtual void BuildHash(FHashBuilder& HashBuilder) override;

	virtual void SerializeMetaData(FActorMetaDataSerializer* Serializer) override;

	TArray<FGuid> SubActors;
	FSoftObjectPath	HLODLayer;
#endif
};
