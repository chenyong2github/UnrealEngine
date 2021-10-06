// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputBehavior.h"
#include "InteractionMechanic.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "GeometryBase.h"
#include "BoxTypes.h"
#include "PlaneTypes.h"
#include "SegmentTypes.h"
#include "VectorTypes.h"

#include "RectangleMarqueeMechanic.generated.h"

class UClickDragInputBehavior;

PREDECLARE_USE_GEOMETRY_CLASS(FGeometrySet3);

/// Struct containing:
///		- camera information, 
///		- input device rays used to define the corners of a rectangle contained in a selection plane, the device ray
///		  screen positions could be used to change the selection behavior when dragging from the top right to bottom
///		  left or vice-versa
/// 
struct MODELINGCOMPONENTS_API FCameraRectangle
{
	using FGeometrySet3 = UE::Geometry::FGeometrySet3;
	using FAxisAlignedBox2 = UE::Geometry::TAxisAlignedBox2<FVector::FReal>;
	using FPlane3 = UE::Geometry::TPlane3<FVector::FReal>;
	using FSegment2 = UE::Geometry::TSegment2<FVector::FReal>;
	using FVector2 = UE::Math::TVector2<FVector::FReal>;

	FViewCameraState CameraState;
	FInputDeviceRay RectangleStartRay = FInputDeviceRay(FRay());
	FInputDeviceRay RectangleEndRay = FInputDeviceRay(FRay());
	FPlane3 CameraPlane;
	FAxisAlignedBox2 RectangleInCameraPlane;

	// This function must be called before other member functions whenever camera state or start/end rays are updated
	void Initialize();
	
	bool bIsInitialized = false;

	// Return an axis aligned box (the region of a marquee selection) in the UV-coordinate system of a plane offset from
	// CameraPlane by the given distance in the CameraPlane normal direction. This function was added in order to
	// implement optimizations (avoid projecting points) when selecting UV editor meshes, which lie in the XY plane
	FAxisAlignedBox2 GetSelectionRectangleUV(double OffsetFromCameraPlane = 0.) const;

	// Return true if the given 3D geometry projected to the camera plane is inside or intersecting the rectangle, and
	// false otherwise
	bool IsProjectedPointInRectangle(const FVector& Point) const;
	bool IsProjectedSegmentIntersectingRectangle(const FVector& Endpoint1, const FVector& Endpoint2) const;
	// bool IsProjectedTriangleIntersectingRectangle(const FVector& A, const FVector& B, const FVector& C) const;

	// Return the 3D point obtained by projecting the given 3D Point onto the given projection plane
	// Note: Assumes an orthographic camera
	static FVector OrthographicProjection(const FPlane3& Plane, const FVector& Point)
	{
		return FVector::PointPlaneProject(Point, (FPlane)Plane);
	}

	// Return the 3D point obtained by projecting the given 3D Point onto the given projection plane
	// Note: OrthographicProjection is more efficient if the camera is known to be orthographic
	FVector PerspectiveProjection(const FPlane3& Plane, const FVector& Point) const
	{
		ensure(bIsInitialized);
		return FMath::RayPlaneIntersection(CameraState.Position, Point - CameraState.Position, (FPlane)Plane);
	}

	// Given a 3D point lying in the given plane, return the UV coordinates of the point expressed in the following a
	// two dimensional parameterization of the given plane:
	// - the 2D origin (0,0) is located at the foot of the projection of cartesian point (0,0,0) onto the plane
	// - the U basis vector is the camera right vector
	// - the V basis vector is the camera up vector
	FVector2 PlaneCoordinates(const FPlane3& Plane, const FVector& PointInPlane) const
	{
		ensure(bIsInitialized);
		FVector::FReal U = FVector::DotProduct(PointInPlane - ((FPlane)Plane).GetOrigin(), CameraState.Right());
		FVector::FReal V = FVector::DotProduct(PointInPlane - ((FPlane)Plane).GetOrigin(), CameraState.Up());
		return FVector2{U, V};
	}
};


/*
 * Mechanic for a rectangle "marquee" selection. It creates and maintains the 2D rectangle associated with a mouse drag. 
 * It does not test against any scene geometry, nor does it maintain any sort of list of selected objects.
 *
 * When using this mechanic, you should call Render() on it in the tool's Render() call so that it can cache
 * necessary camera state, and DrawHUD() in the tool's DrawHUD() call so that it can draw the box.
 *
 * Attach to the mechanic's delegates and use the passed rectangle to test against your geometry. 
 */

UCLASS()
class MODELINGCOMPONENTS_API URectangleMarqueeMechanic : public UInteractionMechanic, public IClickDragBehaviorTarget
{
	GENERATED_BODY()

public:
	/** 
	 * If true, then the URectangleMarqueeMechanic will not create an internal UClickDragInputBehavior in ::Setup(), allowing
	 * the client to control the marquee with an external InputBehavior that uses the marquee mechanic as it's IClickDragBehaviorTarget.
	 * For instance, this allows the mechanic to be hooked in as the drag component of a USingleClickOrDragInputBehavior.
	 * Must be configured before calling ::Setup()
	 */
	UPROPERTY()
	bool bUseExternalClickDragBehavior = false;


public:

	// UInteractionMechanic
	void Setup(UInteractiveTool* ParentTool) override;
	void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI);

	bool IsEnabled();
	void SetIsEnabled(bool bOn);

	/**
	 * Sets the base priority so that users can make sure that their own behaviors are higher
	 * priority. The mechanic will not use any priority value higher than this.
	 * Mechanics could use lower priorities (and their range could be inspected with
	 * GetPriorityRange), but marquee mechanic doesn't. 
	 *
	 * Can be called before or after Setup(). Only relevant if bUseExternalClickDragBehavior is false
	 */
	void SetBasePriority(const FInputCapturePriority& Priority);

	/**
	 * Gets the current priority range used by behaviors in the mechanic, higher
	 * priority to lower.
	 *
	 * For marquee mechanic, the range will be [BasePriority, BasePriority] since
	 * it only uses one priority.
	 */
	TPair<FInputCapturePriority, FInputCapturePriority> GetPriorityRange() const;

	/**
	 * Called when user starts dragging a new rectangle.
	 */
	FSimpleMulticastDelegate OnDragRectangleStarted;

	/**
	 * Called as the user drags the other corner of the rectangle around.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(OnDragRectangleChangedEvent, const FCameraRectangle&);
	OnDragRectangleChangedEvent OnDragRectangleChanged;

	/**
	 * Called once the user lets go of the mouse button after dragging out a rectangle.
	 * The last dragged rectangle is passed here so that clients can choose to just implement this function in simple cases. 
	 * bCancelled flag is true when the drag finishes due to a disabling of the mechanic or due to a TerminateDragSequence call, rather than a normal drag completion.
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(OnDragRectangleFinishedEvent, const FCameraRectangle&, bool bCancelled);
	OnDragRectangleFinishedEvent OnDragRectangleFinished;
	
protected:
	UPROPERTY()
	TObjectPtr<UClickDragInputBehavior> ClickDragBehavior = nullptr;

	FCameraRectangle CameraRectangle;

	FInputCapturePriority BasePriority = FInputCapturePriority(FInputCapturePriority::DEFAULT_TOOL_PRIORITY);

private:

	FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) final;
	void OnClickPress(const FInputDeviceRay& PressPos) final;
	void OnClickDrag(const FInputDeviceRay& DragPos) final;
	void OnClickRelease(const FInputDeviceRay& ReleasePos) final;
	void OnTerminateDragSequence() final;

	bool bIsEnabled;
	bool bIsDragging;
};
