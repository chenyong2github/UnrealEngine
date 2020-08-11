// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartitionEditorCell.generated.h"

class FWorldPartitionActorDesc;
class UWorldPartition;

/**
 * Represents an edition cell (editor-only)
 */
UCLASS(Within = WorldPartitionEditorHash)
class UWorldPartitionEditorCell: public UObject
{
	GENERATED_UCLASS_BODY()

public:

	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	UWorldPartition* GetWorldPartition();

	void AddActor(FWorldPartitionActorDesc* InActorDesc);
	void RemoveActor(FWorldPartitionActorDesc* InActorDesc);

	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;

	FBox						Bounds;
	bool						bDirty : 1;

	/** Tells if the cell was manually loaded in the editor */
	bool						bLoaded : 1;
	bool						bWasLoaded : 1;
	bool						bSelected : 1;
	TSet<FWorldPartitionActorDesc*> Actors;
	TSet<FWorldPartitionActorDesc*> LoadedActors;
#endif
};