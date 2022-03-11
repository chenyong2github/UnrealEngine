// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "SWorldPartitionEditorGrid2D.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Commands/UICommandList.h"

class SWorldPartitionEditorGridSpatialHash : public SWorldPartitionEditorGrid2D
{
public:
	WORLD_PARTITION_EDITOR_IMPL(SWorldPartitionEditorGridSpatialHash);

	SWorldPartitionEditorGridSpatialHash();
	~SWorldPartitionEditorGridSpatialHash();

	void Construct(const FArguments& InArgs);

	int32 PaintGrid(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const override;

private:
	void UpdateWorldMiniMapDetails();
	FBox2D          WorldMiniMapBounds;
	FSlateBrush		WorldMiniMapBrush;
};
