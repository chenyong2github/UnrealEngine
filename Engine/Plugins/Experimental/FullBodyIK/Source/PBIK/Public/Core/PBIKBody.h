// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

struct FPBIKSolverSettings;

namespace PBIK
{

struct FJointConstraint;
struct FRigidBody;
struct FEffector;

struct FBone
{
	FName Name;
	int ParentIndex = -2; // -2 is unset, -1 for root, or 0...n otherwise
	bool bIsSolverRoot = false;
	bool bIsSolved = false;
	bool bIsSubRoot = false;
	FVector Position;
	FQuat Rotation;
	FVector LocalPositionOrig;
	FQuat LocalRotationOrig;

	// initialized - these fields are null/empty until after Solver::Initialize()
	FRigidBody* Body = nullptr;
	FBone* Parent = nullptr;
	TArray<FBone*> Children;
	// initialized

	FBone(
		const FName InName,
		const int& InParentIndex,		// must pass -1 for root of whole skeleton
		const FVector& InOrigPosition,
		const FQuat& InOrigRotation,
		bool bInIsSolverRoot);

	bool HasChild(const FBone* Bone);
};

enum class ELimitType : uint8
{
	Free,
	Limited,
	Locked,
};

struct FBoneSettings
{
	float RotationStiffness = 0.0f; // range (0, 1)
	float PositionStiffness = 0.0f; // range (0, 1)

	ELimitType X;
	float MinX = 0.0f; // range (-180, 180)
	float MaxX = 0.0f;

	ELimitType Y;
	float MinY = 0.0f;
	float MaxY = 0.0f;

	ELimitType Z;
	float MinZ = 0.0f;
	float MaxZ = 0.0f;

	bool bUsePreferredAngles = false;
	FRotator PreferredAngles = FRotator::ZeroRotator;
};

struct FRigidBody
{
	FBone* Bone = nullptr;
	FBoneSettings J;

	FVector Position;
	FQuat Rotation;
	FQuat RotationOrig;
	FVector BoneLocalPosition;
	TArray<FVector> ChildLocalPositions;

	float InvMass = 0.0f;
	FEffector* AttachedEffector = nullptr;
	float Length;
	
private:

	int NumBonesToRoot = 0;

public:

	FRigidBody(FBone* InBone);

	void Initialize(FBone* SolverRoot);

	void UpdateFromInputs(const FPBIKSolverSettings& Settings);

	int GetNumBonesToRoot() const;

	FRigidBody* GetParentBody();

	void ApplyPushToRotateBody(const FVector& Push, const FVector& Offset);
	
	void ApplyPushToPosition(const FVector& Push);
};

// for sorting Bodies hierarchically (root to leaf order)
inline bool operator<(const FRigidBody& Lhs, const FRigidBody& Rhs)
{ 
	return Lhs.GetNumBonesToRoot() < Rhs.GetNumBonesToRoot(); 
}

} // namespace