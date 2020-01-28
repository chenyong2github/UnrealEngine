// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseGizmos/GizmoComponents.h"
#include "AxisPositionGizmo.generated.h"




UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UAxisPositionGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
};


/**
 * UAxisPositionGizmo implements a gizmo interaction where 1D parameter value is manipulated
 * by dragging a point on a 3D line/axis in space. The 3D point is converted to the axis parameter at
 * the nearest point, giving us the 1D parameter value.
 *
 * As with other base gizmos, this class only implements the interaction. The visual aspect of the
 * gizmo, the axis, and the parameter storage are all provided externally.
 *
 * The axis direction+origin is provided by an IGizmoAxisSource. 
 *
 * The interaction target (ie the thing you have to click on to start the dragging interaction) is provided by an IGizmoClickTarget.
 *
 * The new 1D parameter value is sent to an IGizmoFloatParameterSource
 *
 * Internally a UClickDragInputBehavior is used to handle mouse input, configured in ::Setup()
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UAxisPositionGizmo : public UInteractiveGizmo, public IClickDragBehaviorTarget, public IHoverBehaviorTarget
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
	/** AxisSource provides the 3D line on which the interaction happens */
	UPROPERTY()
	TScriptInterface<IGizmoAxisSource> AxisSource;

	/** The 3D line-nearest-point is converted to a 1D coordinate along the line, and the change in value is sent to this ParameterSource */
	UPROPERTY()
	TScriptInterface<IGizmoFloatParameterSource> ParameterSource;

	/** The HitTarget provides a hit-test against some 3D element (presumably a visual widget) that controls when interaction can start */
	UPROPERTY()
	TScriptInterface<IGizmoClickTarget> HitTarget;

	/** StateTarget is notified when interaction starts and ends, so that things like undo/redo can be handled externally. */
	UPROPERTY()
	TScriptInterface<IGizmoStateTarget> StateTarget;


public:
	/** If enabled, then the sign on the parameter delta is always "increasing" when moving away from the origin point, rather than just being a projection onto the axis */
	UPROPERTY()
	bool bEnableSignedAxis = false;


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
	FVector InteractionAxis;

	UPROPERTY()
	FVector InteractionStartPoint;

	UPROPERTY()
	FVector InteractionCurPoint;

	UPROPERTY()
	float InteractionStartParameter;

	UPROPERTY()
	float InteractionCurParameter;

	UPROPERTY()
	float ParameterSign = 1.0f;

protected:
	FVector LastHitPosition;
	float InitialTargetParameter;
};

