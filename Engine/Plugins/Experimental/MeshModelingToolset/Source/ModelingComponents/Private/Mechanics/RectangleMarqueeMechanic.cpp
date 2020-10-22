// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mechanics/RectangleMarqueeMechanic.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "ToolSceneQueriesUtil.h"

#define LOCTEXT_NAMESPACE "URectangleMarqueeMechanic"


static FVector2D PlaneCoordinates(const FVector& Point, const FPlane& Plane, const FVector& UBasisVector, const FVector& VBasisVector)
{
	float U = FVector::DotProduct(Point - Plane.GetOrigin(), UBasisVector);
	float V = FVector::DotProduct(Point - Plane.GetOrigin(), VBasisVector);
	return FVector2D{ U,V };
}

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
	FVector2D Start2D = PlaneCoordinates(StartIntersection,
										 CameraPlane,
										 UBasisVector,
										 VBasisVector);

	FVector CurrentIntersection = FMath::RayPlaneIntersection(DragEndWorldRay.Origin,
															  DragEndWorldRay.Direction,
															  CameraPlane);

	FVector2D Current2D = PlaneCoordinates(CurrentIntersection,
										   CameraPlane,
										   UBasisVector,
										   VBasisVector);

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
		// intersect along the eye-to-point ray
		ProjectedPoint = FMath::RayPlaneIntersection(CameraOrigin,
													 Point - CameraOrigin,
													 CameraPlane);
	}

	FVector2D Point2D = PlaneCoordinates(ProjectedPoint, CameraPlane, UBasisVector, VBasisVector);
	return RectangleCorners.IsInside(Point2D);
}

// ---------------------------------------

void URectangleMarqueeMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	UClickDragInputBehavior* ClickDragBehavior = NewObject<UClickDragInputBehavior>(this);
	ClickDragBehavior->Initialize(this);
	ParentTool->AddInputBehavior(ClickDragBehavior);
}

void URectangleMarqueeMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	// Cache the camera state
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CachedCameraState);
}

FInputRayHit URectangleMarqueeMechanic::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FInputRayHit Dummy;

	// This is the boolean that is checked to see if a drag sequence can be started. In our case we want to begin the 
	// drag sequence even if the first ray doesn't hit anything, so set this to true.
	Dummy.bHit = true;

	return Dummy;
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

	OnDragRectangleStarted();
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

	OnDragRectangleChanged(Rectangle);
}

void URectangleMarqueeMechanic::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	bIsDragging = false;
	OnDragRectangleFinished();
}

void URectangleMarqueeMechanic::OnTerminateDragSequence()
{
	bIsDragging = false;
	OnDragRectangleFinished();
}


void URectangleMarqueeMechanic::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	if (bIsDragging)
	{
		FVector2D Start = DragStartScreenPosition;
		FVector2D Curr = DragCurrentScreenPosition;
		FCanvasBoxItem BoxItem(Start / Canvas->GetDPIScale(), (Curr - Start) / Canvas->GetDPIScale());
		BoxItem.SetColor(FLinearColor::White);
		Canvas->DrawItem(BoxItem);
	}
}

#undef LOCTEXT_NAMESPACE
