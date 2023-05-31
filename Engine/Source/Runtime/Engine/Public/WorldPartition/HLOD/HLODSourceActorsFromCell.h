// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/HLOD/HLODSourceActors.h"
#include "WorldPartition/HLOD/HLODSubActor.h"
#include "HLODSourceActorsFromCell.generated.h"


UCLASS()
class ENGINE_API UWorldPartitionHLODSourceActorsFromCell : public UWorldPartitionHLODSourceActors
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	virtual ULevelStreaming* LoadSourceActors(bool& bOutDirty) const override;
	virtual uint32 GetHLODHash() const override;

	void SetActors(const TArray<FHLODSubActor>& InSourceActors);
	const TArray<FHLODSubActor>& GetActors() const;
#endif

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FHLODSubActor> Actors;
#endif
};
