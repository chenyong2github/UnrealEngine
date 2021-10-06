// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mechanics/RectangleMarqueeMechanic.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "ToolSceneQueriesUtil.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
#include "Intersection/IntrSegment2Segment2.h"
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "URectangleMarqueeMechanic"

void FCameraRectangle::Initialize()
{
	bIsInitialized = true;
	CameraPlane = FPlane3(CameraState.Forward(), CameraState.Position + CameraState.Forward());
	RectangleInCameraPlane = GetSelectionRectangleUV();
}

FCameraRectangle::FAxisAlignedBox2 FCameraRectangle::GetSelectionRectangleUV(double OffsetFromCameraPlane) const
{
	ensure(bIsInitialized);
	
	FAxisAlignedBox2 Result;

	FPlane3 SelectionPlane = CameraPlane;
	SelectionPlane.Constant += OffsetFromCameraPlane;
	
	FVector ProjectedStart = FMath::RayPlaneIntersection(RectangleStartRay.WorldRay.Origin, RectangleStartRay.WorldRay.Direction, (FPlane)SelectionPlane);
	FVector ProjectedEnd = FMath::RayPlaneIntersection(RectangleEndRay.WorldRay.Origin, RectangleEndRay.WorldRay.Direction, (FPlane)SelectionPlane);
	FVector2 StartUV = PlaneCoordinates(SelectionPlane, ProjectedStart);
	FVector2 EndUV = PlaneCoordinates(SelectionPlane, ProjectedEnd);

	// Initialize this way so we don't have to care about min/max
	Result.Contain(StartUV);
	Result.Contain(EndUV);

	return Result;
}

bool FCameraRectangle::IsProjectedPointInRectangle(const FVector& Point) const
{
	ensure(bIsInitialized);
	
	FVector ProjectedPoint;
	if (CameraState.bIsOrthographic)
	{
		ProjectedPoint = OrthographicProjection(CameraPlane, Point);
	}
	else
	{
		// If it's not in front of the camera plane, its not contained in the camera rectangle
		if (CameraPlane.DistanceTo(Point) <= 0)
		{
			return false;
		}
		ProjectedPoint = PerspectiveProjection(CameraPlane, Point);
	}

	FVector2 PointUV = PlaneCoordinates(CameraPlane, ProjectedPoint);
	return RectangleInCameraPlane.Contains(PointUV);
}

bool FCameraRectangle::IsProjectedSegmentIntersectingRectangle(const FVector& Endpoint1, const FVector& Endpoint2) const
{
	ensure(bIsInitialized);
	
	FVector ProjectedEndpoint1;
	FVector ProjectedEndpoint2;

	if (CameraState.bIsOrthographic)
	{
		ProjectedEndpoint1 = OrthographicProjection(CameraPlane, Endpoint1);
		ProjectedEndpoint2 = OrthographicProjection(CameraPlane, Endpoint2);
	}
	else
	{
		ProjectedEndpoint1 = Endpoint1;
		ProjectedEndpoint2 = Endpoint2;

		// We have to crop the segment to the portion in front of the camera plane
		FPlane3::EClipSegmentType ClipType = CameraPlane.ClipSegment(ProjectedEndpoint1, ProjectedEndpoint2);
		
		// Since the selection plane is identical to the clipping plane there's no need to reproject clipped points
		switch (ClipType)
		{
			case FPlane3::FullyClipped:
				return false; // Segment is behind the camera plane hence not in the camera rectangle
			case FPlane3::FirstClipped:
				ProjectedEndpoint2 = PerspectiveProjection(CameraPlane, ProjectedEndpoint2);
				break;
			case FPlane3::SecondClipped:
				ProjectedEndpoint1 = PerspectiveProjection(CameraPlane, ProjectedEndpoint1);
				break;
			case FPlane3::NotClipped:
			default:
				ProjectedEndpoint1 = PerspectiveProjection(CameraPlane, ProjectedEndpoint1);
				ProjectedEndpoint2 = PerspectiveProjection(CameraPlane, ProjectedEndpoint2);
		}
	}

	// TODO Port Wild Magic IntrSegment2Box2 (which requires porting IntrLine2Box2) so we can call segment-box intersection here

	FVector2 Endpoint1UV = PlaneCoordinates(CameraPlane, ProjectedEndpoint1);
	FVector2 Endpoint2UV = PlaneCoordinates(CameraPlane, ProjectedEndpoint2);
	FSegment2 ProjectedSegmentUV(Endpoint1UV, Endpoint2UV);

	// If either endpoint is inside, then definitely (at least partially) contained
	if (RectangleInCameraPlane.Contains(ProjectedSegmentUV.StartPoint()) ||
		RectangleInCameraPlane.Contains(ProjectedSegmentUV.EndPoint()))
	{
		return true;
	}

	// If both outside, have to do some intersections with the box sides

	if (ProjectedSegmentUV.Intersects(FSegment2(RectangleInCameraPlane.GetCorner(0), RectangleInCameraPlane.GetCorner(1))))
	{
		return true;
	}

	if (ProjectedSegmentUV.Intersects(FSegment2(RectangleInCameraPlane.GetCorner(1), RectangleInCameraPlane.GetCorner(2))))
	{
		return true;
	}

	return ProjectedSegmentUV.Intersects(FSegment2(RectangleInCameraPlane.GetCorner(3), RectangleInCameraPlane.GetCorner(2)));

	// Don't need to intersect with the fourth side because segment would have to intersect two sides
	// of box if both endpoints are outside the box.
}

// ---------------------------------------

void URectangleMarqueeMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	if (bUseExternalClickDragBehavior == false)
	{
		ClickDragBehavior = NewObject<UClickDragInputBehavior>(this);
		ClickDragBehavior->SetDefaultPriority(BasePriority);
		ClickDragBehavior->Initialize(this);
		ParentTool->AddInputBehavior(ClickDragBehavior, this);
	}
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
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraRectangle.CameraState);
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

	CameraRectangle.RectangleStartRay = PressPos;
	CameraRectangle.RectangleEndRay = PressPos;
	CameraRectangle.Initialize();

	OnDragRectangleStarted.Broadcast();
}

void URectangleMarqueeMechanic::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if (!DragPos.bHas2D)
	{
		return;
	}

	bIsDragging = true;
	CameraRectangle.RectangleEndRay = DragPos;
	CameraRectangle.Initialize();
	
	OnDragRectangleChanged.Broadcast(CameraRectangle);
}

void URectangleMarqueeMechanic::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	bIsDragging = false;
	OnDragRectangleFinished.Broadcast(CameraRectangle, false);
}

void URectangleMarqueeMechanic::OnTerminateDragSequence()
{
	bIsDragging = false;
	OnDragRectangleFinished.Broadcast(CameraRectangle, true);
}


void URectangleMarqueeMechanic::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	EViewInteractionState State = RenderAPI->GetViewInteractionState();
	bool bThisViewHasFocus = !!(State & EViewInteractionState::Focused);
	if (bThisViewHasFocus && bIsDragging)
	{
		FVector2D Start = CameraRectangle.RectangleStartRay.ScreenPosition;
		FVector2D Curr = CameraRectangle.RectangleEndRay.ScreenPosition;
		FCanvasBoxItem BoxItem(Start / Canvas->GetDPIScale(), (Curr - Start) / Canvas->GetDPIScale());
		BoxItem.SetColor(FLinearColor::White);
		Canvas->DrawItem(BoxItem);
	}
}

#undef LOCTEXT_NAMESPACE
