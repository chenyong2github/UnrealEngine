// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/ParameterSourcesFloat.h"
#include "BaseGizmos/ParameterSourcesVec2.h"
#include "ParameterToTransformAdapters.generated.h"


//
// Various 1D and 2D ParameterSource converters intended to be used to create 3D transformation gizmos.
// Based on base classes in ParameterSourcesFloat.h and ParameterSourcesVec2.h
// 



/**
 * UGizmoAxisTranslationParameterSource is an IGizmoFloatParameterSource implementation that
 * interprets the float value as the parameter of a line equation, and maps this parameter to a 3D translation 
 * along a line with origin/direction given by an IGizmoAxisSource. This translation is applied to an IGizmoTransformSource.
 *
 * This ParameterSource is intended to be used to create 3D Axis Translation Gizmos.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoAxisTranslationParameterSource : public UGizmoBaseFloatParameterSource
{
	GENERATED_BODY()
public:
	virtual float GetParameter() const override
	{
		return Parameter;
	}

	virtual void SetParameter(float NewValue) override
	{
		Parameter = NewValue;
		LastChange.CurrentValue = NewValue;

		// construct translation as delta from initial position
		FVector Translation = LastChange.GetChangeDelta() * CurTranslationAxis;

		// translate the initial transform
		FTransform NewTransform = InitialTransform;
		NewTransform.AddToTranslation(Translation);
		TransformSource->SetTransform(NewTransform);

		OnParameterChanged.Broadcast(this, LastChange);
	}

	virtual void BeginModify()
	{
		check(AxisSource);

		LastChange = FGizmoFloatParameterChange(Parameter);

		InitialTransform = TransformSource->GetTransform();
		CurTranslationAxis = AxisSource->GetDirection();
		CurTranslationOrigin = AxisSource->GetOrigin();
	}

	virtual void EndModify()
	{
	}


public:
	/** The Parameter line-equation value is converted to a 3D Translation along this Axis */
	UPROPERTY()
	TScriptInterface<IGizmoAxisSource> AxisSource;

	/** This TransformSource is updated by applying the constructed 3D translation */
	UPROPERTY()
	TScriptInterface<IGizmoTransformSource> TransformSource;


public:
	/** Parameter is the line-equation parameter that this FloatParameterSource provides */
	UPROPERTY()
	float Parameter = 0.0f;

	/** Active parameter change (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FGizmoFloatParameterChange LastChange;

	/** tranlsation axis for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurTranslationAxis;

	/** tranlsation origin for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurTranslationOrigin;

	/** Saved copy of Initial Transform for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FTransform InitialTransform;

public:

	/**
	 * Create a standard instance of this ParameterSource, with the given AxisSource and TransformSource
	 */
	static UGizmoAxisTranslationParameterSource* Construct(
		IGizmoAxisSource* AxisSourceIn,
		IGizmoTransformSource* TransformSourceIn,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoAxisTranslationParameterSource* NewSource = NewObject<UGizmoAxisTranslationParameterSource>(Outer);
		NewSource->AxisSource = Cast<UObject>(AxisSourceIn);
		NewSource->TransformSource = Cast<UObject>(TransformSourceIn);
		return NewSource;
	}
};





/**
 * UGizmoAxisRotationParameterSource is an IGizmoVec2ParameterSource implementation that
 * interprets the FVector2D parameter as a position in a 2D plane, and maps this position to a 3D translation
 * a plane with origin/normal given by an IGizmoAxisSource. This translation is applied to an IGizmoTransformSource.
 * 
 * This ParameterSource is intended to be used to create 3D Plane Translation Gizmos.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoPlaneTranslationParameterSource : public UGizmoBaseVec2ParameterSource
{
	GENERATED_BODY()
public:
	virtual FVector2D GetParameter() const override
	{
		return Parameter;
	}

	virtual void SetParameter(const FVector2D& NewValue) override
	{
		Parameter = NewValue;
		LastChange.CurrentValue = NewValue;

		// construct translation as delta from initial position
		FVector2D Delta = LastChange.GetChangeDelta();
		FVector Translation = Delta.X*CurTranslationAxisX + Delta.Y*CurTranslationAxisY;

		// apply translation to initial transform
		FTransform NewTransform = InitialTransform;
		NewTransform.AddToTranslation(Translation);
		TransformSource->SetTransform(NewTransform);

		OnParameterChanged.Broadcast(this, LastChange);
	}

	virtual void BeginModify()
	{
		check(AxisSource);

		LastChange = FGizmoVec2ParameterChange(Parameter);

		// save initial transformation and axis information
		InitialTransform = TransformSource->GetTransform();
		CurTranslationOrigin = AxisSource->GetOrigin();
		AxisSource->GetAxisFrame(CurTranslationNormal, CurTranslationAxisX, CurTranslationAxisY);
	}

	virtual void EndModify()
	{
	}


public:
	/** AxisSource provides the 3D plane (origin/normal/u/v) that is used to interpret the 2D parameters */
	UPROPERTY()
	TScriptInterface<IGizmoAxisSource> AxisSource;

	/** This TransformSource is updated by applying the constructed 3D translation */
	UPROPERTY()
	TScriptInterface<IGizmoTransformSource> TransformSource;


public:
	/** Parameter is the two line-equation parameters that this Vec2ParameterSource provides */
	UPROPERTY()
	FVector2D Parameter = FVector2D::ZeroVector;

	/** Active parameter change (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FGizmoVec2ParameterChange LastChange;

	/** plane origin for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurTranslationOrigin;

	/** plane normal for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurTranslationNormal;

	/** in-plane axis X for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurTranslationAxisX;

	/** in-plane axis Y for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurTranslationAxisY;

	/** Saved copy of Initial Transform for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FTransform InitialTransform;

public:

	/**
	 * Create a standard instance of this ParameterSource, with the given AxisSource and TransformSource
	 */
	static UGizmoPlaneTranslationParameterSource* Construct(
		IGizmoAxisSource* AxisSourceIn,
		IGizmoTransformSource* TransformSourceIn,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoPlaneTranslationParameterSource* NewSource = NewObject<UGizmoPlaneTranslationParameterSource>(Outer);
		NewSource->AxisSource = Cast<UObject>(AxisSourceIn);
		NewSource->TransformSource = Cast<UObject>(TransformSourceIn);
		return NewSource;
	}
};






/**
 * UGizmoAxisRotationParameterSource is an IGizmoFloatParameterSource implementation that
 * interprets the float parameter as an angle, and maps this angle to a 3D rotation
 * around an IGizmoAxisSource (ie 3D axis). This rotation is applied to an IGizmoTransformSource.
 * This ParameterSource is intended to be used to create 3D Rotation Gizmos.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoAxisRotationParameterSource : public UGizmoBaseFloatParameterSource
{
	GENERATED_BODY()
public:
	virtual float GetParameter() const override
	{
		return Angle;
	}

	virtual void SetParameter(float NewValue) override
	{
		Angle = NewValue;
		LastChange.CurrentValue = NewValue;

		// construct rotation as delta from initial position
		FQuat DeltaRotation(CurRotationAxis, LastChange.GetChangeDelta());

		// rotate the vector from the rotation origin to the transform origin, 
		// to get the translation of the origin produced by the rotation
		FVector DeltaPosition = InitialTransform.GetLocation() - CurRotationOrigin;
		DeltaPosition = DeltaRotation * DeltaPosition;
		FVector NewLocation = CurRotationOrigin + DeltaPosition;

		// rotate the initial transform by the rotation
		FQuat NewRotation = DeltaRotation * InitialTransform.GetRotation();

		// construct new transform
		FTransform NewTransform = InitialTransform;
		NewTransform.SetLocation(NewLocation);
		NewTransform.SetRotation(NewRotation);
		TransformSource->SetTransform(NewTransform);

		OnParameterChanged.Broadcast(this, LastChange);
	}

	virtual void BeginModify()
	{
		check(AxisSource != nullptr);

		LastChange = FGizmoFloatParameterChange(Angle);

		// save initial transformation and axis information
		InitialTransform = TransformSource->GetTransform();
		CurRotationAxis = AxisSource->GetDirection();
		CurRotationOrigin = AxisSource->GetOrigin();
	}

	virtual void EndModify()
	{
	}


public:
	/** float-parameter Angle is mapped to a 3D Rotation around this Axis */
	UPROPERTY()
	TScriptInterface<IGizmoAxisSource> AxisSource;

	/** This TransformSource is updated by applying the constructed 3D rotation */
	UPROPERTY()
	TScriptInterface<IGizmoTransformSource> TransformSource;


public:
	/** Angle is the parameter that this FloatParameterSource provides */
	UPROPERTY()
	float Angle = 0.0f;

	/** Active parameter change (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FGizmoFloatParameterChange LastChange;

	/** Rotation axis for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurRotationAxis;

	/** Rotation origin for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurRotationOrigin;

	/** Saved copy of Initial Transform for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FTransform InitialTransform;


public:

	/**
	 * Create a standard instance of this ParameterSource, with the given AxisSource and TransformSource
	 */
	static UGizmoAxisRotationParameterSource* Construct(
		IGizmoAxisSource* AxisSourceIn,
		IGizmoTransformSource* TransformSourceIn,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoAxisRotationParameterSource* NewSource = NewObject<UGizmoAxisRotationParameterSource>(Outer);
		NewSource->AxisSource = Cast<UObject>(AxisSourceIn);
		NewSource->TransformSource = Cast<UObject>(TransformSourceIn);
		return NewSource;
	}
};


