// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mechanics/RectangleMarqueeMechanic.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "ToolSceneQueriesUtil.h"
#include "TransformTypes.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "URectangleMarqueeMechanic"


FCameraRectangle::FCameraRectangle(const FViewCameraState& CachedCameraState,
								   const FRay& DragStartWorldRay,
								   const FRay& DragEndWorldRay)
{
	// Create a plane just in front of the camera
	CameraOrigin = CachedCameraState.Position;
	CameraPlane = FPlane(CachedCameraState.Position + CachedCameraState.Forward(), CachedCameraState.Forward());
	bCameraIsOrthographic = CachedCameraState.bIsOrthographic;

	// Intersect the drag rays with the camera plane and compute their coordinates in the camera basis
	UBasisVector = CachedCameraState.Right();
	VBasisVector = CachedCameraState.Up();

	FVector StartIntersection = FMath::RayPlaneIntersection(DragStartWorldRay.Origin,
															DragStartWorldRay.Direction,
															CameraPlane);
	FVector2D Start2D = PlaneCoordinates(StartIntersection);

	FVector CurrentIntersection = FMath::RayPlaneIntersection(DragEndWorldRay.Origin,
															  DragEndWorldRay.Direction,
															  CameraPlane);

	FVector2D Current2D = PlaneCoordinates(CurrentIntersection);

	RectangleCorners = FBox2D(Start2D, Start2D);
	RectangleCorners += Current2D;	// Initialize this way so we don't have to care about min/max
}

bool FCameraRectangle::IsProjectedPointInRectangle(const FVector& Point) const
{
	FVector ProjectedPoint;
	if (bCameraIsOrthographic)
	{
		// project directly to plane
		ProjectedPoint = FVector::PointPlaneProject(Point, CameraPlane);
	}
	else
	{
		// If it's behind the camera rectangle, then not contained.
		if (CameraPlane.PlaneDot(Point) < 0)
		{
			return false;
		}

		// intersect along the eye-to-point ray
		ProjectedPoint = FMath::RayPlaneIntersection(CameraOrigin,
													 Point - CameraOrigin,
													 CameraPlane);
	}

	FVector2D Point2D = PlaneCoordinates(ProjectedPoint);
	return RectangleCorners.IsInside(Point2D);
}

bool FCameraRectangle::IsProjectedSegmentIntersectingRectangle(const FVector& Endpoint1, const FVector& Endpoint2) const
{
	FVector ProjectedEndpoint1;
	FVector ProjectedEndpoint2;

	if (bCameraIsOrthographic)
	{
		ProjectedEndpoint1 = FVector::PointPlaneProject(Endpoint1, CameraPlane);
		ProjectedEndpoint2 = FVector::PointPlaneProject(Endpoint2, CameraPlane);
	}
	else
	{
		// We'll have to crop the segment to the portion in front of the camera plane
		bool bPoint1InFrontOfCamera = CameraPlane.PlaneDot(Endpoint1) > 0;
		bool bPoint2InFrontOfCamera = CameraPlane.PlaneDot(Endpoint2) > 0;
		
		if (!bPoint1InFrontOfCamera && !bPoint2InFrontOfCamera)
		{
			return false; // Segment is behind the camera plane
		}

		FVector IntersectionPoint;
		if (bPoint1InFrontOfCamera != bPoint2InFrontOfCamera)
		{
			// Get the intersection point so we can replace the endpoint that is behind the camera with it.
			ensure(FMath::SegmentPlaneIntersection(Endpoint1, Endpoint2, CameraPlane, IntersectionPoint));
		}

		ProjectedEndpoint1 = bPoint1InFrontOfCamera ? FMath::RayPlaneIntersection(CameraOrigin, Endpoint1 - CameraOrigin, CameraPlane)
			: IntersectionPoint;
		ProjectedEndpoint2 = bPoint2InFrontOfCamera ? FMath::RayPlaneIntersection(CameraOrigin, Endpoint2 - CameraOrigin, CameraPlane)
			: IntersectionPoint;
	}

	FVector2D Endpoint1PlaneCoord = PlaneCoordinates(ProjectedEndpoint1);
	FVector2D Endpoint2PlaneCoord = PlaneCoordinates(ProjectedEndpoint2);

	// If either endpoint is inside, then definitely (at least partially) contained
	if (RectangleCorners.IsInside(Endpoint1PlaneCoord) || RectangleCorners.IsInside(Endpoint2PlaneCoord))
	{
		return true;
	}

	// If both outside, have to do some intersections with the box sides. The function we have for this
	// uses FVectors instead of FVector2Ds.
	ProjectedEndpoint1 = FVector(Endpoint1PlaneCoord, 0);
	ProjectedEndpoint2 = FVector(Endpoint2PlaneCoord, 0);
	FVector Throwaway;
	return FMath::SegmentIntersection2D(ProjectedEndpoint1, ProjectedEndpoint2, 
		FVector(RectangleCorners.Min, 0), FVector(RectangleCorners.Max.X, RectangleCorners.Min.Y, 0), 
		Throwaway)

		|| FMath::SegmentIntersection2D(ProjectedEndpoint1, ProjectedEndpoint2, 
			FVector(RectangleCorners.Max.X, RectangleCorners.Min.Y, 0), FVector(RectangleCorners.Max, 0), 
			Throwaway)

		|| FMath::SegmentIntersection2D(ProjectedEndpoint1, ProjectedEndpoint2,
			FVector(RectangleCorners.Max, 0), FVector(RectangleCorners.Min.X, RectangleCorners.Max.Y, 0), 
			Throwaway);

	// Don't need to intersect with the fourth side because segment would have to intersect two sides
	// of box if both endpoints are outside the box.
}

// ---------------------------------------

void URectangleMarqueeMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	ClickDragBehavior = NewObject<UClickDragInputBehavior>(this);
	ClickDragBehavior->SetDefaultPriority(BasePriority);
	ClickDragBehavior->Initialize(this);
	ParentTool->AddInputBehavior(ClickDragBehavior, this);
	SetIsEnabled(true);
}

bool URectangleMarqueeMechanic::IsEnabled()
{
	return bIsEnabled;
}

void URectangleMarqueeMechanic::SetIsEnabled(bool bOn)
{
	if (bIsDragging && !bOn)
	{
		OnTerminateDragSequence();
	}

	bIsEnabled = bOn;
}

void URectangleMarqueeMechanic::SetBasePriority(const FInputCapturePriority& Priority)
{
	BasePriority = Priority;
	if (ClickDragBehavior)
	{
		ClickDragBehavior->SetDefaultPriority(Priority);
	}
}

TPair<FInputCapturePriority, FInputCapturePriority> URectangleMarqueeMechanic::GetPriorityRange() const
{
	return TPair<FInputCapturePriority, FInputCapturePriority>(BasePriority, BasePriority);
}

void URectangleMarqueeMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	// Cache the camera state
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CachedCameraState);
}

FInputRayHit URectangleMarqueeMechanic::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	return bIsEnabled ? 
		FInputRayHit(TNumericLimits<float>::Max()) // bHit is true. Depth is max to lose the standard tiebreaker.
		: FInputRayHit(); // bHit is false
}

void URectangleMarqueeMechanic::OnClickPress(const FInputDeviceRay& PressPos)
{
	if (!PressPos.bHas2D)
	{
		bIsDragging = false;
		return;
	}

	DragStartScreenPosition = PressPos.ScreenPosition;
	DragStartWorldRay = PressPos.WorldRay;

	OnDragRectangleStarted.Broadcast();
}

void URectangleMarqueeMechanic::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if (!DragPos.bHas2D)
	{
		return;
	}

	bIsDragging = true;
	DragCurrentScreenPosition = DragPos.ScreenPosition;
	FRay DragCurrentWorldRay = DragPos.WorldRay;

	FCameraRectangle Rectangle(CachedCameraState, DragStartWorldRay, DragCurrentWorldRay);

	OnDragRectangleChanged.Broadcast(Rectangle);
}

void URectangleMarqueeMechanic::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	bIsDragging = false;
	OnDragRectangleFinished.Broadcast();
}

void URectangleMarqueeMechanic::OnTerminateDragSequence()
{
	bIsDragging = false;
	OnDragRectangleFinished.Broadcast();
}


void URectangleMarqueeMechanic::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	EViewInteractionState State = RenderAPI->GetViewInteractionState();
	bool bThisViewHasFocus = !!(State & EViewInteractionState::Focused);
	if (bThisViewHasFocus && bIsDragging)
	{
		FVector2D Start = DragStartScreenPosition;
		FVector2D Curr = DragCurrentScreenPosition;
		FCanvasBoxItem BoxItem(Start / Canvas->GetDPIScale(), (Curr - Start) / Canvas->GetDPIScale());
		BoxItem.SetColor(FLinearColor::White);
		Canvas->DrawItem(BoxItem);
	}
}

#undef LOCTEXT_NAMESPACE
