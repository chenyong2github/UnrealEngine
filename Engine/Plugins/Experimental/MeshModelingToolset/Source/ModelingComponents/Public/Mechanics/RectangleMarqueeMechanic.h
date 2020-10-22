// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractionMechanic.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "VectorTypes.h"

#include "RectangleMarqueeMechanic.generated.h"


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

	// TODO: Rectangle vs projected triangle, edge, etc.
};


/*
 * Mechanic for a rectangle "marquee" selection. It creates and maintains the 2D rectangle associated with a mouse drag. 
 * It does not test against any scene geometry, nor does it maintain any sort of list of selected objects.
 *
 * To use this class:
 *	- create a subclass of this class
 *  - override the pure virtual functions. These will be called from this class in response to mouse events
 *  - use the FCameraRectangle passed into RectangleChanged to test against your scene geometry
 *
 * See ULatticeControlPointsMechanic for an example.
 */

UCLASS()
class MODELINGCOMPONENTS_API URectangleMarqueeMechanic : public UInteractionMechanic, public IClickDragBehaviorTarget
{
	GENERATED_BODY()

public:

	// UInteractionMechanic
	void Setup(UInteractiveTool* ParentTool) override;
	void Render(IToolsContextRenderAPI* RenderAPI) override;

	// REVIEW: Should this be added to UInteractionMechanic?
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI);

protected:

	bool bIsDragging;
	FViewCameraState CachedCameraState;
	FVector2D DragStartScreenPosition;
	FRay DragStartWorldRay;
	FVector2D DragCurrentScreenPosition;

private:

	//
	// Override these
	//

	virtual void OnDragRectangleStarted() 
	PURE_VIRTUAL(URectangleMarqueeMechanic::OnDragRectangleStarted, );

	virtual void OnDragRectangleChanged(const FCameraRectangle& CurrentRectangle) 
	PURE_VIRTUAL(URectangleMarqueeMechanic::OnDragRectangleChanged, );

	virtual void OnDragRectangleFinished() 
	PURE_VIRTUAL(URectangleMarqueeMechanic::OnDragRectangleFinished, );


private:
	
	FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) final;
	void OnClickPress(const FInputDeviceRay& PressPos) final;
	void OnClickDrag(const FInputDeviceRay& DragPos) final;
	void OnClickRelease(const FInputDeviceRay& ReleasePos) final;
	void OnTerminateDragSequence() final;
};
