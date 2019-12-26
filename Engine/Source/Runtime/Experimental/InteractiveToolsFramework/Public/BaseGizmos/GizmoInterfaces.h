// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InputState.h"
#include "GizmoInterfaces.generated.h"


//
// UInterfaces for the various UObjects used in the Standard Gizmo Library.
// 



UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoTransformSource : public UInterface
{
	GENERATED_BODY()
};
/**
 * IGizmoTransformSource is an interface which is used to Get/Set an FTransform.
 */
class INTERACTIVETOOLSFRAMEWORK_API IGizmoTransformSource
{
	GENERATED_BODY()
public:
	UFUNCTION()
	virtual FTransform GetTransform() const = 0;

	UFUNCTION()
	virtual void SetTransform(const FTransform& NewTransform) = 0;
};





UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoAxisSource : public UInterface
{
	GENERATED_BODY()
};
/**
 * IGizmoAxisSource is an interface which is used to get information about a 3D Axis.
 * At minimum this includes a 3D Direction Vector and Origin Point.
 * Optionally the implementation may provide two Tangent Vectors which are
 * assumed to be mutually-orthogonal and perpendicular to the Axis Direction
 * (ie that's the normal and the 3 vectors form a coordinate frame).
 */
class INTERACTIVETOOLSFRAMEWORK_API IGizmoAxisSource
{
	GENERATED_BODY()
public:
	/** @return Origin Point of axis */
	UFUNCTION()
	virtual FVector GetOrigin() const = 0;

	/** @return Direction Vector of axis */
	UFUNCTION()
	virtual FVector GetDirection() const = 0;

	/** @return true if this AxisSource has tangent vectors orthogonal to the Direction vector */
	UFUNCTION()
	virtual bool HasTangentVectors() const { return false; }

	/** Get the two tangent vectors that are orthogonal to the Direction vector. 
	 * @warning Only valid if HasTangentVectors() returns true
	 */
	UFUNCTION()
	virtual void GetTangentVectors(FVector& TangentXOut, FVector& TangentYOut) const { }


	/**
	 * Utility function that always returns a 3D coordinate system (ie plane normal and perpendicular axes).
	 * Internally calls GetTangentVectors() if available, otherwise constructs arbitrary mutually perpendicular vectors. 
	 */
	void GetAxisFrame(
		FVector& PlaneNormalOut, FVector& PlaneAxis1Out, FVector& PlaneAxis2Out) const;
};






UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoClickTarget : public UInterface
{
	GENERATED_BODY()
};
/**
 * IGizmoClickTarget is an interface used to provide a ray-object hit test.
 */
class INTERACTIVETOOLSFRAMEWORK_API IGizmoClickTarget
{
	GENERATED_BODY()
public:
	/**
	 * @return FInputRayHit indicating whether or not the target object was hit by the device-ray at ClickPos
	 */
	//UFUNCTION()    // FInputDeviceRay is not USTRUCT because FRay is not USTRUCT
	virtual FInputRayHit IsHit(const FInputDeviceRay& ClickPos) const = 0;

	UFUNCTION()
	virtual void UpdateHoverState(bool bHovering) const = 0;
};




UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoStateTarget : public UInterface
{
	GENERATED_BODY()
};
/**
 * IGizmoStateTarget is an interface that is used to pass notifications about significant gizmo state updates
 */
class INTERACTIVETOOLSFRAMEWORK_API IGizmoStateTarget
{
	GENERATED_BODY()
public:
	/**
	 * BeginUpdate is called before a standard Gizmo begins changing a parameter (via a ParameterSource)
	 */
	UFUNCTION()
	virtual void BeginUpdate() = 0;

	/**
	 * EndUpdate is called when a standard Gizmo is finished changing a parameter (via a ParameterSource)
	 */
	UFUNCTION()
	virtual void EndUpdate() = 0;
};





UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoFloatParameterSource : public UInterface
{
	GENERATED_BODY()
};
/**
 * IGizmoFloatParameterSource provides Get and Set for an arbitrary float-valued parameter.
 */
class INTERACTIVETOOLSFRAMEWORK_API IGizmoFloatParameterSource
{
	GENERATED_BODY()
public:

	/** @return value of parameter */
	UFUNCTION()
	virtual float GetParameter() const = 0;

	/** notify ParameterSource that a parameter modification is about to begin */
	UFUNCTION()
	virtual void BeginModify() = 0;

	/** set value of parameter */
	UFUNCTION()
	virtual void SetParameter(float NewValue) = 0;

	/** notify ParameterSource that a parameter modification is complete */
	UFUNCTION()
	virtual void EndModify() = 0;
};




UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoVec2ParameterSource : public UInterface
{
	GENERATED_BODY()
};
/**
 * IGizmoVec2ParameterSource provides Get and Set for an arbitrary 2D-vector-valued parameter.
 */
class INTERACTIVETOOLSFRAMEWORK_API IGizmoVec2ParameterSource
{
	GENERATED_BODY()
public:

	/** @return value of parameter */
	UFUNCTION()
	virtual FVector2D GetParameter() const = 0;

	/** notify ParameterSource that a parameter modification is about to begin */
	UFUNCTION()
	virtual void BeginModify() = 0;

	/** set value of parameter */
	UFUNCTION()
	virtual void SetParameter(const FVector2D& NewValue) = 0;

	/** notify ParameterSource that a parameter modification is complete */
	UFUNCTION()
	virtual void EndModify() = 0;
};
