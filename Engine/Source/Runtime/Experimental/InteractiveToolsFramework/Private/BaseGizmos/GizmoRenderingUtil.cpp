// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoRenderingUtil.h"


const FSceneView* GizmoRenderingUtil::FindActiveSceneView(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap)
{
	// can we tell focus here?

	const FSceneView* FirstValidView = nullptr;
	const FSceneView* GizmoControlView = nullptr;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			if (View != nullptr)
			{
				if (FirstValidView == nullptr)
				{
					FirstValidView = View;
				}
				if (View->IsPerspectiveProjection())
				{
					GizmoControlView = View;
				}
			}
		}
	}

	if (FirstValidView == nullptr)
	{
		return nullptr;
	}
	if (GizmoControlView == nullptr)
	{
		GizmoControlView = FirstValidView;
	}

	return GizmoControlView;
}



float GizmoRenderingUtil::CalculateLocalPixelToWorldScale(
	const FSceneView* View,
	const FVector& Location)
{
	FVector2D PixelA, PixelB;
	View->WorldToPixel(Location, PixelA);
	FVector OffsetPointWorld = Location + 0.01f*View->GetViewRight() + 0.01f*View->GetViewUp();
	View->WorldToPixel(OffsetPointWorld, PixelB);

	float PixelDelta = (PixelA - PixelB).Size();
	float WorldDelta = (Location - OffsetPointWorld).Size();

	float PixelToWorldScale = WorldDelta / PixelDelta;

	return PixelToWorldScale;
}