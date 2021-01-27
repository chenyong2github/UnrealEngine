// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "WorldPartitionMiniMap.generated.h"

/**
 * A mini map to preview the world in world partition window. (editor-only)
 */
UCLASS(hidecategories = (Actor, Advanced, Display, Events, Object, Attachment, Info, Input, Blueprint, Layers, Tags, Replication), notplaceable)
class ENGINE_API AWorldPartitionMiniMap : public AInfo
{
	GENERATED_BODY()

public:
	AWorldPartitionMiniMap(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool IsEditorOnly() const final { return true; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/*WorldBounds for MinMapTexture*/
	UPROPERTY(VisibleAnywhere, Category = WorldPartitionMiniMap)
	FBox MiniMapWorldBounds;

	/*MiniMap Texture for displaying on world partition window*/
	UPROPERTY(VisibleAnywhere, Category = WorldPartitionMiniMap)
	TObjectPtr<UTexture2D> MiniMapTexture;

	/*MiniMap Size*/
	UPROPERTY(EditAnywhere, Category = WorldPartitionMiniMap, meta=(UIMin = "256", UIMax = "8192"))
	int32 MiniMapSize = 1024;
};