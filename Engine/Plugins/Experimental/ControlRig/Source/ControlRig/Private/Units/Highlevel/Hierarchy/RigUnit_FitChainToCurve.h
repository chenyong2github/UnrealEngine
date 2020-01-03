// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_FitChainToCurve.generated.h"

UENUM()
enum class EControlRigCurveAlignment : uint8
{
	Front,
	Stretched
};

USTRUCT()
struct FRigUnit_FitChainToCurve_Rotation
{
	GENERATED_BODY()

	FRigUnit_FitChainToCurve_Rotation()
	{
		Rotation = FQuat::Identity;
		Ratio = 0.f;
	}

	/**
	 * The rotation to be applied
	 */
	UPROPERTY(meta = (Input))
	FQuat Rotation;

	/**
	 * The ratio of where this rotation sits along the chain
	 */
	UPROPERTY(meta = (Input, Constant))
	float Ratio;
};

USTRUCT()
struct FRigUnit_FitChainToCurve_DebugSettings
{
	GENERATED_BODY()

	FRigUnit_FitChainToCurve_DebugSettings()
	{
		bEnabled = false;
		Scale = 1.f;
		CurveColor = FLinearColor::Yellow;
		SegmentsColor= FLinearColor::Red;
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
	 * The color to use for debug drawing
	 */
	UPROPERTY(meta = (Input))
	FLinearColor CurveColor;

	/**
	 * The color to use for debug drawing
	 */
	UPROPERTY(meta = (Input))
	FLinearColor SegmentsColor;

	/**
	 * The offset at which to draw the debug information in the world
	 */
	UPROPERTY(meta = (Input))
	FTransform WorldOffset;
};

USTRUCT()
struct FRigUnit_FitChainToCurve_WorkData
{
	GENERATED_BODY()

	FRigUnit_FitChainToCurve_WorkData()
	{
		ChainLength = 0.f;
	}

	UPROPERTY()
	float ChainLength;

	UPROPERTY()
	TArray<FVector> BonePositions;

	UPROPERTY()
	TArray<float> BoneSegments;

	UPROPERTY()
	TArray<FVector> CurvePositions;

	UPROPERTY()
	TArray<float> CurveSegments;

	UPROPERTY()
	TArray<int32> BoneIndices;

	UPROPERTY()
	TArray<int32> BoneRotationA;

	UPROPERTY()
	TArray<int32> BoneRotationB;

	UPROPERTY()
	TArray<float> BoneRotationT;

	UPROPERTY()
	TArray<FTransform> BoneLocalTransforms;
};
/**
 * Fits a given chain to a four point bezier curve.
 * Additionally provides rotational control matching the features of the Distribute Rotation node.
 */
USTRUCT(meta=(DisplayName="Fit Chain on Curve", Category="Hierarchy", Keywords="Fit,Resample,Bezier"))
struct FRigUnit_FitChainToCurve : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	FRigUnit_FitChainToCurve()
	{
		StartBone = EndBone = NAME_None;
		Bezier = FCRFourPointBezier();
		Alignment = EControlRigCurveAlignment::Stretched;
		Minimum = 0.f;
		Maximum = 1.f;
		SamplingPrecision = 12;
		PrimaryAxis = FVector(1.f, 0.f, 0.f);
		SecondaryAxis = FVector(0.f, 0.f, 0.f);
		PoleVectorPosition = FVector::ZeroVector;
		RotationEaseType = EControlRigAnimEasingType::Linear;
		bPropagateToChildren = false;
		DebugSettings = FRigUnit_FitChainToCurve_DebugSettings();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** 
	 * The name of the first bone to align
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName StartBone;

	/** 
	 * The name of the last bone to align
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName EndBone;

	/** 
	 * The curve to align to
	 */
	UPROPERTY(meta = (Input))
	FCRFourPointBezier Bezier;

	/** 
	 * Specifies how to align the chain on the curve
	 */
	UPROPERTY(meta = (Input, Constant))
	EControlRigCurveAlignment Alignment;

	/** 
	 * The minimum U value to use on the curve
	 */
	UPROPERTY(meta = (Input, Constant))
	float Minimum;

	/** 
	 * The maximum U value to use on the curve
	 */
	UPROPERTY(meta = (Input, Constant))
	float Maximum;

	/**
	 * The number of samples to use on the curve. Clamped at 64.
	 */
	UPROPERTY(meta = (Input, Constant))
	int32 SamplingPrecision;

	/**
	 * The major axis being aligned - along the bone
	 */
	UPROPERTY(meta = (Input))
	FVector PrimaryAxis;

	/**
	 * The minor axis being aligned - towards the pole vector.
	 * You can use (0.0, 0.0, 0.0) to disable it.
	 */
	UPROPERTY(meta = (Input))
	FVector SecondaryAxis;

	/**
	 * The the position of the pole vector used for aligning the secondary axis.
	 * Only has an effect if the secondary axis is set.
	 */
	UPROPERTY(meta = (Input))
	FVector PoleVectorPosition;

	/** 
	 * The list of rotations to be applied along the curve
	 */
	UPROPERTY(meta = (Input))
	TArray<FRigUnit_FitChainToCurve_Rotation> Rotations;

	/**
	 * The easing to use between to rotations.
	 */
	UPROPERTY(meta = (Input, Constant))
	EControlRigAnimEasingType RotationEaseType;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	UPROPERTY(meta = (Input))
	FRigUnit_FitChainToCurve_DebugSettings DebugSettings;

	UPROPERTY(transient)
	FRigUnit_FitChainToCurve_WorkData WorkData;
};
