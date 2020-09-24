// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetControlTransform.generated.h"

/**
 * SetControlBool is used to perform a change in the hierarchy by setting a single control's bool value.
 */
USTRUCT(meta=(DisplayName="Set Control Bool", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlBool,SetGizmoBool", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlBool : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlBool()
		: BoolValue(false)
		, CachedControlIndex(FCachedRigElement())
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the bool for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
	FName Control;

	/**
	 * The bool value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, Output))
	bool BoolValue;

	// Used to cache the internally used bone index
	UPROPERTY()
	FCachedRigElement CachedControlIndex;
};

USTRUCT()
struct FRigUnit_SetMultiControlBool_Entry
{
	GENERATED_BODY()

	FRigUnit_SetMultiControlBool_Entry()
		: BoolValue(false)
	{}
	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName"))
	FName Control;
	/**
	 * The bool value to set for the given Control.
	 */
	UPROPERTY(meta = (Input))
	bool BoolValue;
};

/**
 * SetMultiControlBool is used to perform a change in the hierarchy by setting multiple controls' bool value.
 */
USTRUCT(meta = (DisplayName = "Set Multiple Controls Bool", Category = "Hierarchy", DocumentationPolicy = "Strict", Keywords = "SetMultipleControlsBool,SetControlBool,SetGizmoBool", PrototypeName = "SetMultiControlValue"))
struct FRigUnit_SetMultiControlBool : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetMultiControlBool()
	{
		Entries.Add(FRigUnit_SetMultiControlBool_Entry());
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The array of control-Bool pairs to be processed
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	TArray<FRigUnit_SetMultiControlBool_Entry> Entries;

	// Used to cache the internally used control indices
	UPROPERTY()
	TArray<FCachedRigElement> CachedControlIndices;
};

/**
 * SetControlFloat is used to perform a change in the hierarchy by setting a single control's float value.
 */
USTRUCT(meta=(DisplayName="Set Control Float", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlFloat,SetGizmoFloat", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlFloat : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlFloat()
		: Weight(1.f)
		, FloatValue(0.f)
		, CachedControlIndex(FCachedRigElement())
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
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

	// Used to cache the internally used control index
	UPROPERTY()
	FCachedRigElement CachedControlIndex;
};

USTRUCT()
struct FRigUnit_SetMultiControlFloat_Entry
{ 
	GENERATED_BODY()

	FRigUnit_SetMultiControlFloat_Entry()
		: FloatValue(0.f)
	{}
	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
	FName Control;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float FloatValue; 
};

/**
 * SetMultiControlFloat is used to perform a change in the hierarchy by setting multiple controls' float value.
 */
USTRUCT(meta = (DisplayName = "Set Multiple Controls Float", Category = "Hierarchy", DocumentationPolicy = "Strict", Keywords = "SetMultipleControlsFloat,SetControlFloat,SetGizmoFloat", PrototypeName = "SetMultiControlValue"))
struct FRigUnit_SetMultiControlFloat : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetMultiControlFloat()
		: Weight(1.f)
	{
		Entries.Add(FRigUnit_SetMultiControlFloat_Entry());
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The array of control-float pairs to be processed
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	TArray<FRigUnit_SetMultiControlFloat_Entry> Entries;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	// Used to cache the internally used control indices
	UPROPERTY()
	TArray<FCachedRigElement> CachedControlIndices;
};



/**
 * SetControlInteger is used to perform a change in the hierarchy by setting a single control's int32 value.
 */
USTRUCT(meta=(DisplayName="Set Control Integer", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlInteger,SetGizmoInteger", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlInteger : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlInteger()
		: Weight(1.f)
		, IntegerValue(0)
		, CachedControlIndex(FCachedRigElement())
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
	FName Control;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	int32 Weight;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, Output))
	int32 IntegerValue;

	// Used to cache the internally used bone index
	UPROPERTY()
	FCachedRigElement CachedControlIndex;
};

USTRUCT()
struct FRigUnit_SetMultiControlInteger_Entry
{
	GENERATED_BODY()

	FRigUnit_SetMultiControlInteger_Entry()
		: IntegerValue(0)
	{}
	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName"))
	FName Control;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input))
	int32 IntegerValue;
};

/**
 * SetMultiControlInteger is used to perform a change in the hierarchy by setting multiple controls' integer value.
 */
USTRUCT(meta = (DisplayName = "Set Multiple Controls Integer", Category = "Hierarchy", DocumentationPolicy = "Strict", Keywords = "SetMultipleControlsInteger,SetControlInteger,SetGizmoInteger", PrototypeName = "SetMultiControlValue"))
struct FRigUnit_SetMultiControlInteger : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetMultiControlInteger()
		: Weight(1.f)
	{
		Entries.Add(FRigUnit_SetMultiControlInteger_Entry());
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The array of control-integer pairs to be processed
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	TArray<FRigUnit_SetMultiControlInteger_Entry> Entries;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	// Used to cache the internally used control indices
	UPROPERTY()
	TArray<FCachedRigElement> CachedControlIndices;
};

/**
 * SetControlVector2D is used to perform a change in the hierarchy by setting a single control's Vector2D value.
 */
USTRUCT(meta=(DisplayName="Set Control Vector2D", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlVector2D,SetGizmoVector2D", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlVector2D : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlVector2D()
		: Weight(1.f)
		, Vector(FVector2D::ZeroVector)
		, CachedControlIndex(FCachedRigElement())
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
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
	FCachedRigElement CachedControlIndex;
};

USTRUCT()
struct FRigUnit_SetMultiControlVector2D_Entry
{
	GENERATED_BODY()

	FRigUnit_SetMultiControlVector2D_Entry()
		: Vector(FVector2D::ZeroVector)
	{}

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName"))
	FName Control;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input))
	FVector2D Vector;
};

/**
 * SetMultiControlVector2D is used to perform a change in the hierarchy by setting multiple controls' vector2D value.
 */
USTRUCT(meta = (DisplayName = "Set Multiple Controls Vector2D", Category = "Hierarchy", DocumentationPolicy = "Strict", Keywords = "SetMultipleControlsVector2D,SetControlVector2D,SetGizmoVector2D", PrototypeName = "SetMultiControlValue"))
struct FRigUnit_SetMultiControlVector2D : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetMultiControlVector2D()
		: Weight(1.f)
	{
		Entries.Add(FRigUnit_SetMultiControlVector2D_Entry());
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The array of control-vector2D pairs to be processed
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	TArray<FRigUnit_SetMultiControlVector2D_Entry> Entries;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	// Used to cache the internally used control indices
	UPROPERTY()
	TArray<FCachedRigElement> CachedControlIndices;
};

/**
 * SetControlVector is used to perform a change in the hierarchy by setting a single control's Vector value.
 */
USTRUCT(meta=(DisplayName="Set Control Vector", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlVector,SetGizmoVector", PrototypeName = "SetControlValue", Deprecated = "4.25"))
struct FRigUnit_SetControlVector : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlVector()
		: Weight(1.f)
		, Vector(FVector::OneVector)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, CachedControlIndex(FCachedRigElement())
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
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
	FCachedRigElement CachedControlIndex;
};

/**
 * SetControlRotator is used to perform a change in the hierarchy by setting a single control's Rotator value.
 */
USTRUCT(meta=(DisplayName="Set Control Rotator", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlRotator,SetGizmoRotator", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlRotator : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlRotator()
		: Weight(1.f)
		, Rotator(FRotator::ZeroRotator)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, CachedControlIndex(FCachedRigElement())
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
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
	FCachedRigElement CachedControlIndex;
};

USTRUCT()
struct FRigUnit_SetMultiControlRotator_Entry
{
	GENERATED_BODY()

	FRigUnit_SetMultiControlRotator_Entry()
	{
		Rotator = FRotator::ZeroRotator;
		Space = EBoneGetterSetterMode::GlobalSpace;
	}

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName"))
	FName Control;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input))
	FRotator Rotator;

	/**
	 * Defines if the bone's transform should be set
	 * in local or global space.
	 */
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;
};

/**
 * SetMultiControlRotator is used to perform a change in the hierarchy by setting multiple controls' rotator value.
 */
USTRUCT(meta = (DisplayName = "Set Multiple Controls Rotator", Category = "Hierarchy", DocumentationPolicy = "Strict", Keywords = "SetMultipleControlsRotator,SetControlRotator,SetGizmoRotator", PrototypeName = "SetMultiControlValue"))
struct FRigUnit_SetMultiControlRotator : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetMultiControlRotator()
		: Weight(1.f)
	{
		Entries.Add(FRigUnit_SetMultiControlRotator_Entry());
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The array of control-rotator pairs to be processed
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	TArray<FRigUnit_SetMultiControlRotator_Entry> Entries;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	// Used to cache the internally used control indices
	UPROPERTY()
	TArray<FCachedRigElement> CachedControlIndices;
};

/**
 * SetControlTransform is used to perform a change in the hierarchy by setting a single control's transform.
 */
USTRUCT(meta=(DisplayName="Set Control Transform", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlTransform,SetGizmoTransform", PrototypeName = "SetControlValue", Deprecated = "4.25"))
struct FRigUnit_SetControlTransform : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlTransform()
		: Weight(1.f)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, CachedControlIndex(FCachedRigElement())
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
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
	FCachedRigElement CachedControlIndex;
};
