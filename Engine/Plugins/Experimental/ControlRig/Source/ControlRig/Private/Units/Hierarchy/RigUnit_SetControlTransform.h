// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetControlTransform.generated.h"

/**
 * SetControlBool is used to perform a change in the hierarchy by setting a single control's bool value.
 */
USTRUCT(meta=(DisplayName="Set Control Bool", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlBool", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlBool : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlBool()
		: BoolValue(false)
		, CachedControlIndex(INDEX_NONE)
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the bool for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName", Constant))
	FName Control;

	/**
	 * The bool value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, Output))
	bool BoolValue;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedControlIndex;
};

/**
 * SetControlFloat is used to perform a change in the hierarchy by setting a single control's float value.
 */
USTRUCT(meta=(DisplayName="Set Control Float", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlFloat", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlFloat : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlFloat()
		: Weight(1.f)
		, FloatValue(0.f)
		, CachedControlIndex(INDEX_NONE)
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName", Constant))
	FName Control;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, Output, UIMin = "0.0", UIMax = "1.0"))
	float FloatValue;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedControlIndex;
};

/**
 * SetControlVector2D is used to perform a change in the hierarchy by setting a single control's Vector2D value.
 */
USTRUCT(meta=(DisplayName="Set Control Vector2D", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlVector2D", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlVector2D : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlVector2D()
		: Weight(1.f)
		, Vector(FVector2D::ZeroVector)
		, CachedControlIndex(INDEX_NONE)
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName", Constant))
	FName Control;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, Output))
	FVector2D Vector;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedControlIndex;
};

/**
 * SetControlVector is used to perform a change in the hierarchy by setting a single control's Vector value.
 */
USTRUCT(meta=(DisplayName="Set Control Vector", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlVector", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlVector : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlVector()
		: Weight(1.f)
		, Vector(FVector::OneVector)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, CachedControlIndex(INDEX_NONE)
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName", Constant))
	FName Control;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, Output))
	FVector Vector;

	/**
	 * Defines if the bone's transform should be set
	 * in local or global space.
	 */
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedControlIndex;
};

/**
 * SetControlRotator is used to perform a change in the hierarchy by setting a single control's Rotator value.
 */
USTRUCT(meta=(DisplayName="Set Control Rotator", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlRotator", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlRotator : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlRotator()
		: Weight(1.f)
		, Rotator(FRotator::ZeroRotator)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, CachedControlIndex(INDEX_NONE)
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName", Constant))
	FName Control;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, Output))
	FRotator Rotator;

	/**
	 * Defines if the bone's transform should be set
	 * in local or global space.
	 */
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedControlIndex;
};

/**
 * SetControlTransform is used to perform a change in the hierarchy by setting a single control's transform.
 */
USTRUCT(meta=(DisplayName="Set Control Transform", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlTransform", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlTransform : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlTransform()
		: Weight(1.f)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, CachedControlIndex(INDEX_NONE)
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName", Constant))
	FName Control;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, Output))
	FTransform Transform;

	/**
	 * Defines if the bone's transform should be set
	 * in local or global space.
	 */
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedControlIndex;
};
