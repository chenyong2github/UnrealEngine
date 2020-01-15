// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoRenderingUtil.h"
#include "RHI.h"


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


static double VectorDifferenceSqr(const FVector2D& A, const FVector2D& B)
{
	double ax = A.X, ay = A.Y;
	double bx = B.X, by = B.Y;
	ax -= bx;
	ay -= by;
	return ax*ax + ay*ay;
}

static double VectorDifferenceSqr(const FVector& A, const FVector& B)
{
	double ax = A.X, ay = A.Y, az = A.Z;
	double bx = B.X, by = B.Y, bz = B.Z;
	ax -= bx; 
	ay -= by;
	az -= bz;
	return ax*ax + ay*ay + az*az;
}

// duplicates FSceneView::WorldToPixel but in double where possible (unfortunately WorldToScreen still in float)
static FVector2D WorldToPixelDouble(const FSceneView* View, const FVector& Location)
{
	FVector4 ScreenPoint = View->WorldToScreen(Location);

	double InvW = (ScreenPoint.W > 0.0 ? 1.0 : -1.0) / (double)ScreenPoint.W;
	double Y = (GProjectionSignY > 0.0) ? (double)ScreenPoint.Y : 1.0 - (double)ScreenPoint.Y;

	const FIntRect& UnscaledViewRect = View->UnscaledViewRect;
	double PosX = (double)UnscaledViewRect.Min.X + (0.5 + (double)ScreenPoint.X * 0.5 * InvW) * (double)UnscaledViewRect.Width();
	double PosY = (double)UnscaledViewRect.Min.Y + (0.5 - Y * 0.5 * InvW) * (double)UnscaledViewRect.Height();

	return FVector2D((float)PosX, (float)PosY);
}


float GizmoRenderingUtil::CalculateLocalPixelToWorldScale(
	const FSceneView* View,
	const FVector& Location)
{
	// To calculate this scale at Location, we project Location to screen and also project a second
	// point at a small distance from Location in a camera-perpendicular plane, then measure 2D/3D distance ratio. 
	// However, because some of the computations are done in float, there will be enormous numerical error 
	// when the camera is very far from the location if the distance is relatively small. The "W" value
	// below gives us a sense of this distance, so we make the offset relative to that
	// (this does do one redundant WorldToScreen)
	FVector4 LocationScreenPoint = View->WorldToScreen(Location);
	float OffsetDelta = LocationScreenPoint.W * 0.01f;

	FVector2D PixelA = WorldToPixelDouble(View, Location);
	FVector OffsetPointWorld = Location + OffsetDelta*View->GetViewRight() + OffsetDelta*View->GetViewUp();
	FVector2D PixelB = WorldToPixelDouble(View, OffsetPointWorld);

	double PixelDeltaSqr = VectorDifferenceSqr(PixelA, PixelB);
	double WorldDeltaSqr = VectorDifferenceSqr(Location, OffsetPointWorld);
	return (float)(sqrt(WorldDeltaSqr / PixelDeltaSqr));
}