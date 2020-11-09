// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoRenderingUtil.h"
#include "RHI.h"


// yuck global value set by Editor
static const FSceneView* GlobalCurrentSceneView = nullptr;

static FCriticalSection GlobalCurrentSceneViewLock;

#if WITH_EDITOR
static bool bGlobalUseCurrentSceneViewTracking = true;
#else
static bool bGlobalUseCurrentSceneViewTracking = false;
#endif


void GizmoRenderingUtil::SetGlobalFocusedEditorSceneView(const FSceneView* View)
{
	GlobalCurrentSceneViewLock.Lock();
	GlobalCurrentSceneView = View;
	GlobalCurrentSceneViewLock.Unlock();
}

void GizmoRenderingUtil::SetGlobalFocusedSceneViewTrackingEnabled(bool bEnabled)
{
	bGlobalUseCurrentSceneViewTracking = bEnabled;
}


const FSceneView* GizmoRenderingUtil::FindFocusedEditorSceneView(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap)
{
	// we are likely being called from a rendering thread GetDynamicMeshElements() function
	const FSceneView* GlobalEditorView = nullptr;
	GlobalCurrentSceneViewLock.Lock();
	GlobalEditorView = GlobalCurrentSceneView;
	GlobalCurrentSceneViewLock.Unlock();

	// if we only have one view, and we are not tracking active view, just use that one
	if (bGlobalUseCurrentSceneViewTracking == false && Views.Num() == 1)
	{
		return Views[0];
	}

	// otherwise try to find the view that the game thread set for us
	int32 VisibleViews = 0;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			VisibleViews++;
			if (Views[ViewIndex] == GlobalEditorView)
			{
				return Views[ViewIndex];
			}
		}
	}

	// if we did not find our view, but only one view is visible, we can speculatively return that one
	if (bGlobalUseCurrentSceneViewTracking == false && VisibleViews == 1)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				return Views[ViewIndex];
			}
		}
	}

	// ok give up
	if (bGlobalUseCurrentSceneViewTracking == false)
	{
		return Views[0];
	}
	else
	{
		return nullptr;
	}
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