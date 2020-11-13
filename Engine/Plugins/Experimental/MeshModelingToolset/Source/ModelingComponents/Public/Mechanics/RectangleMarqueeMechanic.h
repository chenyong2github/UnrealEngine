// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputBehavior.h"
#include "InteractionMechanic.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "TransformTypes.h"
#include "VectorTypes.h"

#include "RectangleMarqueeMechanic.generated.h"

class UClickDragInputBehavior;

/// Struct containing:
///		- camera information, 
///		- a 3D plane just in front of the camera,
///		- a 2D basis for coordinates in this plane, and
///		- the corners of a rectangle contained in this plane, in this 2D basis
/// 
struct FCameraRectangle
{
	FCameraRectangle(const FViewCameraState& CachedCameraState,
					 const FRay& DragStartWorldRay,
					 const FRay& DragEndWorldRay);

	FVector CameraOrigin;
	bool bCameraIsOrthographic;
	FPlane CameraPlane;
	FVector UBasisVector;
	FVector VBasisVector;
	FBox2D RectangleCorners;

	// Project the given 3D point to the camera plane and test if it's in the rectangle
	bool IsProjectedPointInRectangle(const FVector& Point) const;

	// Project the given segment to the camera plane and test if it intersects the rectangle
	bool IsProjectedSegmentIntersectingRectangle(const FVector& Endpoint1, const FVector& Endpoint2) const;

	// TODO: Add a way to test rectangle against triangles.

private:
	FCameraRectangle() {};
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
	 * Can be called before or after Setup().
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
	 */
	FSimpleMulticastDelegate OnDragRectangleFinished;
	
protected:
	UPROPERTY()
	UClickDragInputBehavior* ClickDragBehavior = nullptr;

	FViewCameraState CachedCameraState;

	FInputCapturePriority BasePriority = FInputCapturePriority(FInputCapturePriority::DEFAULT_TOOL_PRIORITY);

private:

	FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) final;
	void OnClickPress(const FInputDeviceRay& PressPos) final;
	void OnClickDrag(const FInputDeviceRay& DragPos) final;
	void OnClickRelease(const FInputDeviceRay& ReleasePos) final;
	void OnTerminateDragSequence() final;

	bool bIsEnabled;
	bool bIsDragging;
	FVector2D DragStartScreenPosition;
	FRay DragStartWorldRay;
	FVector2D DragCurrentScreenPosition;
};
