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
	
	float DistToSubRootOrig;
	FRigidBody* ParentSubRoot = nullptr;

	float TransformAlpha;
	float StrengthAlpha;

	FEffector(FBone* InBone);

	void SetGoal(const FVector InPositionGoal, const FQuat& InRotationGoal, float InTransformAlpha, float InStrengthAlpha);

	void UpdateFromInputs();

	void SquashSubRoots();
};

} // namespace

USTRUCT()
struct FPBIKSolverSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = SolverSettings, meta = (ClampMin = "0", UIMin = "0.0", UIMax = "200.0"))
	int32 Iterations = 20;

	UPROPERTY(EditAnywhere, Category = SolverSettings, meta = (ClampMin = "0", UIMin = "0.0", UIMax = "10.0"))
	float MassMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, Category = SolverSettings)
	bool bAllowStretch = false;

	UPROPERTY(EditAnywhere, Category = SolverSettings)
	bool bPinRoot = false;
};



USTRUCT()
struct FPBIKSolver
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

	bool IsReadyToSimulate();

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

	void InitConstraints();

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
