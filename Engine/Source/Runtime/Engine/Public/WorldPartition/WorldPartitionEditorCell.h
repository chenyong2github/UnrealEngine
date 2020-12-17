// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartitionEditorCell.generated.h"

class FWorldPartitionActorDesc;
class UWorldPartition;

/**
 * Represents an editing cell (editor-only)
 */
UCLASS(Within = WorldPartitionEditorHash)
class UWorldPartitionEditorCell: public UObject
{
	GENERATED_UCLASS_BODY()

public:
	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	void AddActor(const FWorldPartitionHandle& ActorHandle);
	void RemoveActor(const FWorldPartitionHandle& ActorHandle);

	FBox						Bounds;

	/** Tells if the cell was manually loaded in the editor */
	bool						bLoaded : 1;

	TSet<FWorldPartitionHandle> Actors;
	TSet<FWorldPartitionReference> LoadedActors;
#endif
};