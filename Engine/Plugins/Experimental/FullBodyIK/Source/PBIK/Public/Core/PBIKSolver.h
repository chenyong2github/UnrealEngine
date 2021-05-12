// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PBIKBody.h"
#include "PBIKConstraint.h"
#include "PBIKDebug.h"

#include "PBIKSolver.generated.h"

DECLARE_CYCLE_STAT(TEXT("PBIK Solve"), STAT_PBIK_Solve, STATGROUP_Anim);

namespace PBIK
{

static float GLOBAL_UNITS = 100.0f; // (1.0f = meters), (100.0f = centimeters)

struct FRigidBody;

struct FEffector
{
	FVector Position;
	FQuat Rotation;

	FVector PositionOrig;
	FQuat RotationOrig;

	FVector PositionGoal;
	FQuat RotationGoal;

	FBone* Bone;
	TWeakPtr<FPinConstraint> Pin;
	FRigidBody* ParentSubRoot = nullptr;
	float LengthOfChainInInputPose;

	float TransformAlpha;
	float StrengthAlpha;

	FEffector(FBone* InBone);

	void SetGoal(
		const FVector& InPositionGoal,
		const FQuat& InRotationGoal,
		float InTransformAlpha,
		float InStrengthAlpha);

	void UpdateFromInputs();

	void SquashSubRoots();
};

} // namespace

USTRUCT()
struct PBIK_API FPBIKSolverSettings
{
	GENERATED_BODY()

	/** High iteration counts can help solve complex joint configurations with competing constraints, but will increase runtime cost. Default is 20. */
	UPROPERTY(EditAnywhere, Category = SolverSettings, meta = (ClampMin = "0", UIMin = "0.0", UIMax = "200.0"))
	int32 Iterations = 20;

	/** A global mass multiplier; higher values will make the joints more stiff, but require more iterations. Typical range is 0.0 to 10.0. */
	UPROPERTY(EditAnywhere, Category = SolverSettings, meta = (ClampMin = "0", UIMin = "0.0", UIMax = "10.0"))
	float MassMultiplier = 1.0f;

	/** If true, joints will translate to reach the effectors; causing bones to lengthen if necessary. Good for cartoon effects. Default is false. */
	UPROPERTY(EditAnywhere, Category = SolverSettings)
	bool bAllowStretch = false;

	/** Lock the position and rotation of the solver root bone in-place (at animated position). Useful for partial-body solves. Default is false. */
	UPROPERTY(EditAnywhere, Category = SolverSettings)
	bool bPinRoot = false;

	/** When true, the solver is reset each tick to start from the current input pose. If false, incoming animated poses are ignored and the solver starts from the results of the previous solve. Default is true. */
	UPROPERTY(EditAnywhere, Category = SolverSettings)
	bool bStartSolveFromInputPose = true;
};

USTRUCT()
struct PBIK_API FPBIKSolver
{
	GENERATED_BODY()

public:

	PBIK::FDebugDraw* GetDebugDraw();

	//
	// main runtime functions
	//

	bool Initialize();

	void Solve(const FPBIKSolverSettings& Settings);

	void Reset();

	bool IsReadyToSimulate() const;

	//
	// set input / get output at runtime
	//

	void SetBoneTransform(const int32 Index, const FTransform& InTransform);

	PBIK::FBoneSettings* GetBoneSettings(const int32 Index);

	void SetEffectorGoal(
		const int32 Index, 
		const FVector& InPosition, 
		const FQuat& InRotation, 
		const float OffsetAlpha, 
		const float StrengthAlpha);

	void GetBoneGlobalTransform(const int32 Index, FTransform& OutTransform);

	int32 GetNumBones() const { return Bones.Num(); }

	int32 GetBoneIndex(FName BoneName) const;

	//
	// pre-init /  setup functions
	//

	int32 AddBone(
		const FName Name,
		const int32 ParentIndex,
		const FVector& InOrigPosition,
		const FQuat& InOrigRotation,
		bool bIsSolverRoot);

	int32 AddEffector(FName BoneName);
	
private:

	bool InitBones();

	bool InitBodies();

	bool InitConstraints();

	void AddBodyForBone(PBIK::FBone* Bone);

private:

	PBIK::FBone* SolverRoot = nullptr;
	TWeakPtr<PBIK::FPinConstraint> RootPin = nullptr;
	TArray<PBIK::FBone> Bones;
	TArray<PBIK::FRigidBody> Bodies;
	TArray<TSharedPtr<PBIK::FConstraint>> Constraints;
	TArray<PBIK::FEffector> Effectors;
	bool bReadyToSimulate = false;
	
	PBIK::FDebugDraw DebugDraw;
	friend PBIK::FDebugDraw;
};
