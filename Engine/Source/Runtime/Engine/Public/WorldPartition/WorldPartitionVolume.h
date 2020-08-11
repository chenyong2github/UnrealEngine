// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "WorldPartitionVolume.generated.h"

/**
 * A world partition volume to allow loading cells inside (editor-only)
 */
UCLASS()
class ENGINE_API AWorldPartitionVolume : public AVolume
{
	GENERATED_BODY()

public:
	AWorldPartitionVolume(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Begin AActor
	virtual bool IsEditorOnly() const final { return true; }
	// End AActor

#if WITH_EDITOR
	void LoadIntersectingCells();
	virtual EActorGridPlacement GetDefaultGridPlacement() const override { return EActorGridPlacement::AlwaysLoaded; }
#endif
};