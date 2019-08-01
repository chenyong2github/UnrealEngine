// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "RigUnit_TwoBoneIKSimple.generated.h"

USTRUCT()
struct FRigUnit_TwoBoneIKSimple_DebugSettings
{
	GENERATED_BODY()

	FRigUnit_TwoBoneIKSimple_DebugSettings()
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
 * Aligns the rotation of a primary and secondary axis of a bone to a world target.
 * Note: This node operates in world space!
 */
USTRUCT(meta=(DisplayName="Basic IK", Category="Hierarchy", Keywords="TwoBone,IK"))
struct FRigUnit_TwoBoneIKSimple : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	FRigUnit_TwoBoneIKSimple()
	{
		BoneA = BoneB = EffectorBone = PoleVectorSpace = NAME_None;
		Effector = FTransform::Identity;
		PrimaryAxis = FVector(1.f, 0.f, 0.f);
		SecondaryAxis = FVector(0.f, 1.f, 0.f);
		PoleVector = FVector(0.f, 0.f, 1.f);
		PoleVectorKind = EControlRigVectorKind::Direction;
		bEnableStretch = false;
		StretchStartRatio = 0.75f;
		StretchMaximumRatio = 1.25f;
		Weight = 1.f;
		BoneALength = BoneBLength = 0.f;
		bPropagateToChildren = false;
		DebugSettings = FRigUnit_TwoBoneIKSimple_DebugSettings();

		BoneAIndex = INDEX_NONE;
		BoneBIndex = INDEX_NONE;
		EffectorBoneIndex = INDEX_NONE;
		PoleVectorSpaceIndex = INDEX_NONE;
	}

	MULTIPLEX_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** 
	 * The name of first bone
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName BoneA;

	/** 
	 * The name of second bone
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName BoneB;

	/** 
	 * The name of the effector bone (if exists)
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName EffectorBone;

	/** 
	 * The transform of the effector
	 */
	UPROPERTY(meta = (Input))
	FTransform Effector;

	/** 
	 * The major axis being aligned - along the bone
	 */
	UPROPERTY(meta = (Input))
	FVector PrimaryAxis;

	/** 
	 * The minor axis being aligned - towards the polevector
	 */
	UPROPERTY(meta = (Input))
	FVector SecondaryAxis;

	/** 
	 * The polevector to use for the IK solver
	 * This can be a location or direction.
	 */
	UPROPERTY(meta = (Input))
	FVector PoleVector;

	/**
	 * The kind of pole vector this is representing - can be a direction or a location
	 */
	UPROPERTY(meta = (Input))
	EControlRigVectorKind PoleVectorKind;

	/** 
	 * The space in which the pole vector is expressed
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName PoleVectorSpace;

	/**
	 * If set to true the stretch feature of the solver will be enabled
	 */
	UPROPERTY(meta = (Input))
	bool bEnableStretch;

	/**
	 * The ratio where the stretch starts
	 */
	UPROPERTY(meta = (Input))
	float StretchStartRatio;

	/**
     * The maximum allowed stretch ratio
	 */
	UPROPERTY(meta = (Input))
	float StretchMaximumRatio;

	/** 
	 * The weight of the solver - how much the IK should be applied.
	 */
	UPROPERTY(meta = (Input))
	float Weight;

	/** 
	 * The length of the first bone.
	 * If set to 0.0 it will be determined by the hierarchy
	 */
	UPROPERTY(meta = (Input))
	float BoneALength;

	/** 
	 * The length of the second  bone.
	 * If set to 0.0 it will be determined by the hierarchy
	 */
	UPROPERTY(meta = (Input))
	float BoneBLength;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	/** 
	 * The settings for debug drawing
	 */
	UPROPERTY(meta = (Input))
	FRigUnit_TwoBoneIKSimple_DebugSettings DebugSettings;

	UPROPERTY()
	int32 BoneAIndex;
	UPROPERTY()
	int32 BoneBIndex;
	UPROPERTY()
	int32 EffectorBoneIndex;
	UPROPERTY()
	int32 PoleVectorSpaceIndex;
};
