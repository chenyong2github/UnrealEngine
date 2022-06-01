// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
#include "WorldPartitionMiniMap.generated.h"

/**
 * A mini map to preview the world in world partition window. (editor-only)
 */
UCLASS(hidecategories = (Actor, Advanced, Display, Events, Object, Attachment, Info, Input, Blueprint, Layers, Tags, Replication), notplaceable)
class ENGINE_API AWorldPartitionMiniMap : public AInfo
{
	GENERATED_BODY()

private:
#if WITH_EDITOR
	virtual bool ActorTypeSupportsDataLayer() const final { return false; }
#endif

public:
	AWorldPartitionMiniMap(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool IsEditorOnly() const final { return true; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool IsLockLocation() const { return true; }
	virtual bool IsUserManaged() const final { return false; }
	virtual void CheckForErrors() override;
#endif

	/*WorldBounds for MinMapTexture*/
	UPROPERTY(VisibleAnywhere, Category = WorldPartitionMiniMap)
	FBox MiniMapWorldBounds;

	/* UVOffset used to setup Virtual Texture */
	UPROPERTY(EditAnywhere, Category = WorldPartitionMiniMap)
	FBox2D UVOffset;

	/*MiniMap Texture for displaying on world partition window*/
	UPROPERTY(VisibleAnywhere, Category = WorldPartitionMiniMap)
	TObjectPtr<UTexture2D> MiniMapTexture;

	/*Datalayers excluded from MiniMap rendering*/
	UPROPERTY(EditAnywhere, Category = WorldPartitionMiniMap)
	TSet<FActorDataLayer> ExcludedDataLayers;

	/*MiniMap Tile Size*/
	UPROPERTY(EditAnywhere, Category = WorldPartitionMiniMap, meta = (UIMin = "256", UIMax = "8192"))
	int32 MiniMapTileSize = 1024;
};