// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Math/RigUnit_MathBase.h"
#include "RBF/RBFInterpolator.h"

#include "RigUnit_MathRBFInterpolate.generated.h"

enum class EControlRigState : uint8;
template<typename T> class TRBFInterpolator;
struct FRigUnit_MathRBFQuatWeightFunctor;
struct FRigUnit_MathRBFVectorWeightFunctor;


/** Function to use for each target falloff */
UENUM()
enum class ERBFKernelType : uint8
{
	Gaussian,
	Exponential,
	Linear,
	Cubic,
	Quintic
};

/** Function to use for computing distance between the input and target 
	quaternions. */
UENUM()
enum class ERBFQuatDistanceType : uint8
{
	Euclidean,
	ArcLength,
	SwingAngle,
	TwistAngle,
};

/** Function to use for computing distance between the input and target 
	quaternions. */
UENUM()
enum class ERBFVectorDistanceType : uint8
{
	Euclidean,
	Manhattan,
	ArcLength
};


USTRUCT()
struct FRigUnit_MathRBFInterpolateQuatWorkData
{
	GENERATED_BODY()

	// There's no current mechanism for detecting whether an entire input stream is
	// constant or variable in the RigVM. So what we do here is that the firs time
	// around we set up an interpolator 
	TRBFInterpolator<FQuat> Interpolator;
	TArray<FQuat> Targets;
	uint64 Hash = 0;
	bool bAreTargetsConstant = true;
};

USTRUCT()
struct FRigUnit_MathRBFInterpolateVectorWorkData
{
	GENERATED_BODY()

	TRBFInterpolator<FVector> Interpolator;
	TArray<FVector> Targets;
	uint64 Hash = 0;
	bool bAreTargetsConstant = true;
};


USTRUCT(meta = (Abstract, Category = "Math|RBF Interpolation"))
struct FRigUnit_MathRBFInterpolateBase : 
	public FRigUnit_MathBase
{
	GENERATED_BODY()
};

USTRUCT(meta = (Abstract, Keywords = "RBF,Interpolate,Quaternion"))
struct FRigUnit_MathRBFInterpolateQuatBase :
	public FRigUnit_MathRBFInterpolateBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FQuat Input = FQuat::Identity;

	UPROPERTY(meta = (Input, Constant))
	ERBFQuatDistanceType DistanceFunction = ERBFQuatDistanceType::ArcLength;

	UPROPERTY(meta = (Input, Constant))
	ERBFKernelType SmoothingFunction = ERBFKernelType::Gaussian;

	UPROPERTY(meta = (Input, Constant))
	float SmoothingAngle = 45.0f;

	UPROPERTY(meta = (Input, Constant))
	bool bNormalizeOutput = false;

	UPROPERTY(meta = (Input, EditCondition = "DistanceFunction == ERBFQuatDistanceType::SwingAngle || DistanceFunction == ERBFQuatDistanceType::TwistAngle"))
	FVector TwistAxis = FVector::ForwardVector;

	UPROPERTY(transient)
	FRigUnit_MathRBFInterpolateQuatWorkData WorkData;

protected:
	template<typename T>
	static void GetInterpolatedWeights(
		EControlRigState State,
		FRigUnit_MathRBFInterpolateQuatWorkData& WorkData,
		const FRigVMFixedArray<T>& Targets,
		const FQuat& Input,
		ERBFQuatDistanceType DistanceFunction,
		ERBFKernelType SmoothingFunction,
		float SmoothingAngle,
		bool bNormalizeOutput,
		FVector TwistAxis,
		TArray<float, TMemStackAllocator<>>& Weights
	);

	static uint64 HashTargets(const FRigVMFixedArray<FQuat>& Targets);
};

USTRUCT(meta = (Abstract, Keywords = "RBF,Interpolate,Vector"))
struct FRigUnit_MathRBFInterpolateVectorBase :
	public FRigUnit_MathRBFInterpolateBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FVector Input = FVector::ZeroVector;

	UPROPERTY(meta = (Input, Constant))
	ERBFVectorDistanceType DistanceFunction = ERBFVectorDistanceType::Euclidean;

	UPROPERTY(meta = (Input, Constant))
	ERBFKernelType SmoothingFunction = ERBFKernelType::Gaussian;

	UPROPERTY(meta = (Input, Constant))
	float SmoothingRadius = 5.0f;

	UPROPERTY(meta = (Input, Constant))
	bool bNormalizeOutput = false;

	UPROPERTY(transient)
	FRigUnit_MathRBFInterpolateVectorWorkData WorkData;

protected:
	template<typename T>
	static void GetInterpolatedWeights(
		EControlRigState State,
		FRigUnit_MathRBFInterpolateVectorWorkData& WorkData,
		const FRigVMFixedArray<T>& Targets,
		const FVector& Input,
		ERBFVectorDistanceType DistanceFunction,
		ERBFKernelType SmoothingFunction,
		float SmoothingRadius,
		bool bNormalizeOutput,
		TArray<float, TMemStackAllocator<>>& Weights
	);

	static uint64 HashTargets(const FRigVMFixedArray<FVector>& Targets);
};

// The actual unit implementation declarations.

// Quat -> T

USTRUCT()
struct FMathRBFInterpolateQuatFloat_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FQuat Target = FQuat::Identity;

	UPROPERTY(meta = (Input))
	float Value = 0.0f;
};

USTRUCT(meta = (Abstract, DisplayName="Quaternion to Float", PrototypeName="RBFInterpolateQuatToFloat"))
struct FRigUnit_MathRBFInterpolateQuatFloat : 
	public FRigUnit_MathRBFInterpolateQuatBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateQuatFloat_Target> Targets;

	UPROPERTY(meta = (Output))
	float Output = 0.0f;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};


USTRUCT()
struct FMathRBFInterpolateQuatVector_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FQuat Target = FQuat::Identity;

	UPROPERTY(meta = (Input))
	FVector Value = FVector::ZeroVector;
};

USTRUCT(meta = (Abstract, DisplayName = "Quaternion to Vector", PrototypeName = "RBFInterpolateQuatToVector"))
struct FRigUnit_MathRBFInterpolateQuatVector :
	public FRigUnit_MathRBFInterpolateQuatBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateQuatVector_Target> Targets;

	UPROPERTY(meta = (Output))
	FVector Output = FVector::ZeroVector;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};


USTRUCT()
struct FMathRBFInterpolateQuatColor_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FQuat Target = FQuat::Identity;

	UPROPERTY(meta = (Input))
	FLinearColor Value = FLinearColor::Transparent;
};

USTRUCT(meta = (Abstract, DisplayName = "Quaternion to Color", PrototypeName = "RBFInterpolateQuatToColor"))
struct FRigUnit_MathRBFInterpolateQuatColor :
	public FRigUnit_MathRBFInterpolateQuatBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateQuatColor_Target> Targets;

	UPROPERTY(meta = (Output))
	FLinearColor Output = FLinearColor::Transparent;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};


USTRUCT()
struct FMathRBFInterpolateQuatQuat_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FQuat Target = FQuat::Identity;

	UPROPERTY(meta = (Input))
	FQuat Value = FQuat::Identity;
};

USTRUCT(meta = (Abstract, DisplayName = "Quaternion to Quaternion", PrototypeName = "RBFInterpolateQuatToQuat"))
struct FRigUnit_MathRBFInterpolateQuatQuat :
	public FRigUnit_MathRBFInterpolateQuatBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateQuatQuat_Target> Targets;

	UPROPERTY(meta = (Output))
	FQuat Output = FQuat::Identity;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};


USTRUCT()
struct FMathRBFInterpolateQuatXform_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FQuat Target = FQuat::Identity;

	UPROPERTY(meta = (Input))
	FTransform Value = FTransform::Identity;
};

USTRUCT(meta = (Abstract, DisplayName = "Quaternion to Transform", PrototypeName = "RBFInterpolateQuatToXform"))
struct FRigUnit_MathRBFInterpolateQuatXform :
	public FRigUnit_MathRBFInterpolateQuatBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateQuatXform_Target> Targets;

	UPROPERTY(meta = (Output))
	FTransform Output = FTransform::Identity;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};


/// Vector->T

USTRUCT()
struct FMathRBFInterpolateVectorFloat_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FVector Target = FVector::ZeroVector;

	UPROPERTY(meta = (Input))
	float Value = 0.0f;
};

USTRUCT(meta = (Abstract, DisplayName = "Vector to Float", PrototypeName = "RBFInterpolateVectorToFloat", Keywords = "RBF,Interpolate,Vector"))
struct FRigUnit_MathRBFInterpolateVectorFloat :
	public FRigUnit_MathRBFInterpolateVectorBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateVectorFloat_Target> Targets;

	UPROPERTY(meta = (Output))
	float Output = 0.0f;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};


USTRUCT()
struct FMathRBFInterpolateVectorVector_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FVector Target = FVector::ZeroVector;

	UPROPERTY(meta = (Input))
	FVector Value = FVector::ZeroVector;
};

USTRUCT(meta = (Abstract, DisplayName = "Vector to Vector", PrototypeName = "RBFInterpolateVectorToVector", Keywords = "RBF,Interpolate,Vector"))
struct FRigUnit_MathRBFInterpolateVectorVector :
	public FRigUnit_MathRBFInterpolateVectorBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateVectorVector_Target> Targets;

	UPROPERTY(meta = (Output))
	FVector Output = FVector::ZeroVector;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};


USTRUCT()
struct FMathRBFInterpolateVectorColor_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FVector Target = FVector::ZeroVector;

	UPROPERTY(meta = (Input))
	FLinearColor Value = FLinearColor::Transparent;
};

USTRUCT(meta = (Abstract, DisplayName = "Vector to Color", PrototypeName = "RBFInterpolateVectorToColor", Keywords = "RBF,Interpolate,Vector"))
struct FRigUnit_MathRBFInterpolateVectorColor :
	public FRigUnit_MathRBFInterpolateVectorBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateVectorColor_Target> Targets;

	UPROPERTY(meta = (Output))
	FLinearColor Output = FLinearColor::Transparent;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};



USTRUCT()
struct FMathRBFInterpolateVectorQuat_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FVector Target = FVector::ZeroVector;

	UPROPERTY(meta = (Input))
	FQuat Value = FQuat::Identity;
};

USTRUCT(meta = (Abstract, DisplayName = "Vector to Quat", PrototypeName = "RBFInterpolateVectorToQuat", Keywords = "RBF,Interpolate,Vector"))
struct FRigUnit_MathRBFInterpolateVectorQuat :
	public FRigUnit_MathRBFInterpolateVectorBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateVectorQuat_Target> Targets;

	UPROPERTY(meta = (Output))
	FQuat Output = FQuat::Identity;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};

USTRUCT()
struct FMathRBFInterpolateVectorXform_Target
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	FVector Target = FVector::ZeroVector;

	UPROPERTY(meta = (Input))
	FTransform Value = FTransform::Identity;
};

USTRUCT(meta = (Abstract, DisplayName = "Vector to Transform", PrototypeName = "RBFInterpolateVectorToXform", Keywords = "RBF,Interpolate,Vector"))
struct FRigUnit_MathRBFInterpolateVectorXform :
	public FRigUnit_MathRBFInterpolateVectorBase
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TArray<FMathRBFInterpolateVectorXform_Target> Targets;

	UPROPERTY(meta = (Output))
	FTransform Output = FTransform::Identity;

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;
};
