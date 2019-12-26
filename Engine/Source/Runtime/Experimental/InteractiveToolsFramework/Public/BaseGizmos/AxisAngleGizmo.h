// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseGizmos/GizmoComponents.h"
#include "AxisAngleGizmo.generated.h"




UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UAxisAngleGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
};


/**
 *
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UAxisAngleGizmo : public UInteractiveGizmo, public IClickDragBehaviorTarget, public IHoverBehaviorTarget
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

	// IModifierToggleBehaviorTarget implementation (inherited via IClickDragBehaviorTarget)
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

public:
	UPROPERTY()
	TScriptInterface<IGizmoAxisSource> AxisSource;

	UPROPERTY()
	TScriptInterface<IGizmoFloatParameterSource> AngleSource;

	UPROPERTY()
	TScriptInterface<IGizmoClickTarget> HitTarget;

	UPROPERTY()
	TScriptInterface<IGizmoStateTarget> StateTarget;

public:
	UPROPERTY()
	bool bInInteraction = false;

	UPROPERTY()
	FVector RotationOrigin;
	
	UPROPERTY()
	FVector RotationAxis;

	UPROPERTY()
	FVector RotationPlaneX;

	UPROPERTY()
	FVector RotationPlaneY;


	UPROPERTY()
	FVector InteractionStartPoint;

	UPROPERTY()
	FVector InteractionCurPoint;

	UPROPERTY()
	float InteractionStartAngle;

	UPROPERTY()
	float InteractionCurAngle;

protected:
	FVector LastHitPosition;
	float InitialTargetAngle;

	static const int SnapAngleModifierID = 1;		// identifier we associate with the shift key
	bool bEnableSnapAngleModifier = false;			// flag we use to keep track of modifier state

};

