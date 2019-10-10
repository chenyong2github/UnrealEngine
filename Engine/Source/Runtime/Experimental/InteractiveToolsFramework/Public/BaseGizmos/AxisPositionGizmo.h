// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
 * TODO rename to UAxisPositionGizmo
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
	UPROPERTY()
	TScriptInterface<IGizmoAxisSource> AxisSource;

	UPROPERTY()
	TScriptInterface<IGizmoFloatParameterSource> ParameterSource;

	UPROPERTY()
	TScriptInterface<IGizmoClickTarget> HitTarget;

	UPROPERTY()
	TScriptInterface<IGizmoStateTarget> StateTarget;

public:
	UPROPERTY()
	bool bInInteraction = false;

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


protected:
	FVector LastHitPosition;
	float InitialTargetParameter;
};

