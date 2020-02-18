// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseGizmos/GizmoComponents.h"
#include "PlanePositionGizmo.generated.h"




UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UPlanePositionGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
};


/**
 * UPlanePositionGizmo implements a gizmo interaction where 2D parameter value is manipulated
 * by dragging a point on a 3D plane in space. The 3D position is converted to 2D coordinates
 * based on the tangent axes of the plane.
 * 
 * As with other base gizmos, this class only implements the interaction. The visual aspect of the
 * gizmo, the plane, and the parameter storage are all provided externally.
 *
 * The plane is provided by an IGizmoAxisSource. The origin and normal define the plane and then
 * the tangent axes of the source define the coordinate space. 
 * 
 * The interaction target (ie the thing you have to click on to start the dragging interaction) is provided by an IGizmoClickTarget. 
 *
 * The new 2D parameter value is sent to an IGizmoVec2ParameterSource
 *
 * Internally a UClickDragInputBehavior is used to handle mouse input, configured in ::Setup()
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UPlanePositionGizmo : public UInteractiveGizmo, public IClickDragBehaviorTarget, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	// UInteractiveGizmo overrides

	virtual void Setup() override;

	// IClickDragBehaviorTarget implementation

	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

	// IHoverBehaviorTarget implementation
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

public:
	/** AxisSource provides the 3D plane on which the interaction happens */
	UPROPERTY()
	TScriptInterface<IGizmoAxisSource> AxisSource;

	/** The 3D plane coordinates are converted to 2D coordinates in the plane tangent space, and the change in value is sent to this ParameterSource */
	UPROPERTY()
	TScriptInterface<IGizmoVec2ParameterSource> ParameterSource;
	
	/** The HitTarget provides a hit-test against some 3D element (presumably a visual widget) that controls when interaction can start */
	UPROPERTY()
	TScriptInterface<IGizmoClickTarget> HitTarget;

	/** StateTarget is notified when interaction starts and ends, so that things like undo/redo can be handled externally */
	UPROPERTY()
	TScriptInterface<IGizmoStateTarget> StateTarget;

public:
	/** If enabled, then the sign on the parameter delta is always "increasing" when moving away from the origin point, rather than just being a projection onto the axis */
	UPROPERTY()
	bool bEnableSignedAxis = false;

	/** If enabled, flip sign of parameter delta on X axis */
	UPROPERTY()
	bool bFlipX = false;

	/** If enabled, flip sign of parameter delta on Y axis */
	UPROPERTY()
	bool bFlipY = false;


public:
	/** If true, we are in an active click+drag interaction, otherwise we are not */
	UPROPERTY()
	bool bInInteraction = false;


	//
	// The values below are used in the context of a single click-drag interaction, ie if bInInteraction = true
	// They otherwise should be considered uninitialized
	//

	UPROPERTY()
	FVector InteractionOrigin;

	UPROPERTY()
	FVector InteractionNormal;

	UPROPERTY()
	FVector InteractionAxisX;

	UPROPERTY()
	FVector InteractionAxisY;


	UPROPERTY()
	FVector InteractionStartPoint;

	UPROPERTY()
	FVector InteractionCurPoint;

	UPROPERTY()
	FVector2D InteractionStartParameter;

	UPROPERTY()
	FVector2D InteractionCurParameter;

	UPROPERTY()
	FVector2D ParameterSigns = FVector2D(1, 1);

protected:
	FVector LastHitPosition;
	FVector2D InitialTargetParameter;
};

