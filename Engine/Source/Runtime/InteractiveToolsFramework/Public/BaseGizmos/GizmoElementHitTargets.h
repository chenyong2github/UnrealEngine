// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/HitTargets.h"
#include "GizmoElementHitTargets.generated.h"

class UGizmoElementBase;

/**
 * UGizmoElementHitTarget is an IGizmoClickTarget implementation that
 * hit-tests any object derived from UGizmoElementBase
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementHitTarget : public UObject, public IGizmoClickTarget
{
	GENERATED_BODY()
public:

	/**
	 * Gizmo element.
	 */
	UPROPERTY()
	TObjectPtr<UGizmoElementBase> GizmoElement;

	/**
	 * If set, this condition is checked before performing the hit test. This gives a way
	 * to disable the hit test without hiding the component. This is useful, for instance,
	 * in a repositionable transform gizmo in world-coordinate mode, where the rotation
	 * components need to be hittable for movement, but not for repositioning.
	 */
	TFunction<bool(const FInputDeviceRay&)> Condition = nullptr;

public:
	virtual FInputRayHit IsHit(const FInputDeviceRay& ClickPos) const;

	virtual void UpdateHoverState(bool bHovering);

	virtual void UpdateInteractingState(bool bInteracting);

public:
	static UGizmoElementHitTarget* Construct(
		UGizmoElementBase* InGizmoElement,
		UObject* Outer = (UObject*)GetTransientPackage());
};

