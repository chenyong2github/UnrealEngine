// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "RigUnit_SpringIK.generated.h"

USTRUCT()
struct FRigUnit_SpringIK_DebugSettings
{
	GENERATED_BODY()

	FRigUnit_SpringIK_DebugSettings()
	{
		bEnabled = false;
		Scale = 10.f;
		WorldOffset = FTransform::Identity;
	}

	/**
	 * If enabled debug information will be drawn 
	 */
	UPROPERTY(meta = (Input))
	bool bEnabled;

	/**
	 * The size of the debug drawing information
	 */
	UPROPERTY(meta = (Input))
	float Scale;

	/**
	 * The offset at which to draw the debug information in the world
	 */
	UPROPERTY(meta = (Input))
	FTransform WorldOffset;
};

/**
 * The Spring IK solver uses a verlet integrator to perform an IK solve.
 * It support custom constraints including distance, length etc.
 * Note: This node operates in world space!
 */
USTRUCT(meta=(DisplayName="Spring IK", Category="Hierarchy", Keywords="IK"))
struct FRigUnit_SpringIK : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	FRigUnit_SpringIK()
	{
		Bone = NAME_None;
		Primary = FRigUnit_SpringIK_Target();
		Secondary = FRigUnit_SpringIK_Target();
		Primary.Axis = FVector(1.f, 0.f, 0.f);
		Secondary.Axis = FVector(0.f, 0.f, 1.f);
		bPropagateToChildren = false;
		DebugSettings = FRigUnit_SpringIK_DebugSettings();
		BoneIndex = INDEX_NONE;
		PrimaryCachedSpaceName = NAME_None;
		PrimaryCachedSpaceIndex = INDEX_NONE;
		SecondaryCachedSpaceName = NAME_None;
		SecondaryCachedSpaceIndex = INDEX_NONE;
	}

	virtual void Execute(const FRigUnitContext& Context) override;

	/** 
	 * The name of the bones to solve
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	TArray<FName> Bones;

	/**
	 * The primary target for the aim 
	 */
	UPROPERTY(meta = (Input))
	FRigUnit_SpringIK_Target Primary;

	/**
	 * The secondary target for the aim - also referred to as PoleVector / UpVector
	 */
	UPROPERTY(meta = (Input))
	FRigUnit_SpringIK_Target Secondary;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	/** The debug setting for the node */
	UPROPERTY(meta = (Input))
	FRigUnit_SpringIK_DebugSettings DebugSettings;

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
