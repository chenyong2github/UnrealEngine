// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "HitTargets.generated.h"

class UPrimitiveComponent;


/**
 * UGizmoLambdaHitTarget is an IGizmoClickTarget implementation that
 * forwards the hit-test function to a TFunction
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoLambdaHitTarget : public UObject, public IGizmoClickTarget
{
	GENERATED_BODY()
public:
	/** This function is called to determine if target is hit */
	TUniqueFunction<FInputRayHit(const FInputDeviceRay&)> IsHitFunction;

	/** This function is called to update hover state of the target */
	TFunction<void(bool)> UpdateHoverFunction;


public:
	virtual FInputRayHit IsHit(const FInputDeviceRay& ClickPos) const
	{
		if (IsHitFunction)
		{
			return IsHit(ClickPos);
		}
		return FInputRayHit();
	}

	virtual void UpdateHoverState(bool bHovering) const
	{
		if (UpdateHoverFunction)
		{
			UpdateHoverFunction(bHovering);
		}
	}
};



/**
 * UGizmoComponentHitTarget is an IGizmoClickTarget implementation that
 * hit-tests a UPrimitiveComponent
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoComponentHitTarget : public UObject, public IGizmoClickTarget
{
	GENERATED_BODY()
public:

	/**
	 * Component->LineTraceComponent() is called to determine if the target is hit
	 */
	UPROPERTY()
	UPrimitiveComponent* Component;

	/** This function is called to update hover state of the target */
	TFunction<void(bool)> UpdateHoverFunction;


public:
	virtual FInputRayHit IsHit(const FInputDeviceRay& ClickPos) const;

	virtual void UpdateHoverState(bool bHovering) const
	{
		if (UpdateHoverFunction)
		{
			UpdateHoverFunction(bHovering);
		}
	}

public:
	static UGizmoComponentHitTarget* Construct(
		UPrimitiveComponent* Component,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoComponentHitTarget* NewHitTarget = NewObject<UGizmoComponentHitTarget>(Outer);
		NewHitTarget->Component = Component;
		return NewHitTarget;
	}
};

