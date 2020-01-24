// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "RigUnit_AimBone.generated.h"

USTRUCT()
struct FRigUnit_AimBone_Target
{
	GENERATED_BODY()

	FRigUnit_AimBone_Target()
	{
		Weight = 1.f;
		Axis = FVector(1.f, 0.f, 0.f);
		Target = FVector(1.f, 0.f, 0.f);
		Kind = EControlRigVectorKind::Direction;
		Space = NAME_None;
	}

	/**
	 * The amount of aim rotation to apply on this target.
	 */
	UPROPERTY(EditAnywhere, meta = (Input), Category = "AimTarget")
	float Weight;

	/**
	 * The axis to align with the aim on this target
	 */
	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "Weight > 0.0"), Category = "AimTarget")
	FVector Axis;

	/**
	 * The target to aim at - can be a direction or location based on the Kind setting
	 */
	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "Weight > 0.0"), Category = "AimTarget")
	FVector Target;

	/**
	 * The kind of target this is representing - can be a direction or a location
	 */
	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "Weight > 0.0"), Category = "AimTarget")
	EControlRigVectorKind Kind;

	/**
	 * The space in which the target is expressed
	 */
	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "Weight > 0.0", CustomWidget = "BoneName", Constant), Category = "AimTarget")
	FName Space;
};

USTRUCT()
struct FRigUnit_AimBone_DebugSettings
{
	GENERATED_BODY()

	FRigUnit_AimBone_DebugSettings()
	{
		bEnabled = false;
		Scale = 10.f;
		WorldOffset = FTransform::Identity;
	}

	/**
	 * If enabled debug information will be drawn 
	 */
	UPROPERTY(EditAnywhere, meta = (Input), Category = "DebugSettings")
	bool bEnabled;

	/**
	 * The size of the debug drawing information
	 */
	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "bEnabled"), Category = "DebugSettings")
	float Scale;

	/**
	 * The offset at which to draw the debug information in the world
	 */
	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "bEnabled"), Category = "DebugSettings")
	FTransform WorldOffset;
};

/**
 * Aligns the rotation of a primary and secondary axis of a bone to a world target.
 * Note: This node operates in world space!
 */
USTRUCT(meta=(DisplayName="Aim", Category="Hierarchy", Keywords="Lookat"))
struct FRigUnit_AimBone : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	FRigUnit_AimBone()
	{
		Bone = NAME_None;
		Primary = FRigUnit_AimBone_Target();
		Secondary = FRigUnit_AimBone_Target();
		Primary.Axis = FVector(1.f, 0.f, 0.f);
		Secondary.Axis = FVector(0.f, 0.f, 1.f);
		Weight = 1.f;
		bPropagateToChildren = false;
		DebugSettings = FRigUnit_AimBone_DebugSettings();
		BoneIndex = INDEX_NONE;
		PrimaryCachedSpaceName = NAME_None;
		PrimaryCachedSpaceIndex = INDEX_NONE;
		SecondaryCachedSpaceName = NAME_None;
		SecondaryCachedSpaceIndex = INDEX_NONE;
	}

	virtual FName DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const override
	{
		if (InPinPath.StartsWith(TEXT("Primary.Target")))
		{
			return Primary.Space;
		}
		if (InPinPath.StartsWith(TEXT("Secondary.Target")))
		{
			return Secondary.Space;
		}
		return NAME_None;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** 
	 * The name of the bone to align
	 */
	UPROPERTY(meta = (Input, Constant, CustomWidget = "BoneName"))
	FName Bone;

	/**
	 * The primary target for the aim 
	 */
	UPROPERTY(meta = (Input))
	FRigUnit_AimBone_Target Primary;

	/**
	 * The secondary target for the aim - also referred to as PoleVector / UpVector
	 */
	UPROPERTY(meta = (Input))
	FRigUnit_AimBone_Target Secondary;

	/**
	 * The weight of the change - how much the change should be applied
	 */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input, Constant))
	bool bPropagateToChildren;

	/** The debug setting for the node */
	UPROPERTY(meta = (Input))
	FRigUnit_AimBone_DebugSettings DebugSettings;

	UPROPERTY()
	int32 BoneIndex;

	UPROPERTY()
	FName PrimaryCachedSpaceName;

	UPROPERTY()
	int32 PrimaryCachedSpaceIndex;

	UPROPERTY()
	FName SecondaryCachedSpaceName;

	UPROPERTY()
	int32 SecondaryCachedSpaceIndex;
};
