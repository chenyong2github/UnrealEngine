// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/PBIKBody.h"
#include "Core/PBIKSolver.h"

//#pragma optimize("", off)

namespace PBIK
{

FBone::FBone(
	const FName InName,
	const int& InParentIndex,		// must pass -1 for root of whole skeleton
	const FVector& InOrigPosition,
	const FQuat& InOrigRotation,
	bool bInIsSolverRoot)
{
	Name = InName;
	ParentIndex = InParentIndex;
	Position = InOrigPosition;
	Rotation = InOrigRotation;
	bIsSolverRoot = bInIsSolverRoot;
	bIsSolved = false; // default
}

bool FBone::HasChild(const FBone* Bone)
{
	for (const FBone* Child : Children)
	{
		if (Bone->Name == Child->Name)
		{
			return true;
		}
	}

	return false;
}

FRigidBody::FRigidBody(FBone* InBone)
{
	Bone = InBone;
	J = FBoneSettings();
}

void FRigidBody::Initialize(FBone* SolverRoot)
{
	FVector Centroid = Bone->Position;
	Length = 0.0f;
	for(const FBone* Child : Bone->Children)
	{
		Centroid += Child->Position;
		Length += (Bone->Position - Child->Position).Size();
	}
	Centroid = Centroid * (1.0f / ((float)Bone->Children.Num() + 1.0f));

	Position = Centroid;
	Rotation = RotationOrig = Bone->Rotation;
	BoneLocalPosition = Rotation.Inverse() * (Bone->Position - Centroid);

	for (FBone* Child : Bone->Children)
	{
		FVector ChildLocalPos = Rotation.Inverse() * (Child->Position - Centroid);
		ChildLocalPositions.Add(ChildLocalPos);
	}

	// calculate num bones distance to root
	NumBonesToRoot = 0;
	FBone* Parent = Bone;
	while (Parent && Parent != SolverRoot)
	{
		NumBonesToRoot += 1;
		Parent = Parent->Parent;
	}
}

void FRigidBody::UpdateFromInputs(const FPBIKSolverSettings& Settings)
{
	if (Settings.bStartSolveFromInputPose)
	{
		// set to input pose
		Position = Bone->Position - Bone->Rotation * BoneLocalPosition;
		Rotation = Bone->Rotation;
	}

	// Body.Length used as rough approximation of the mass of the body
	// for fork joints (multiple solved children) we sum lengths to all children (see Initialize)
	InvMass = 1.0f / ( Length * ((Settings.MassMultiplier * GLOBAL_UNITS) + 0.5f));
}

int FRigidBody::GetNumBonesToRoot() const
{ 
	return NumBonesToRoot; 
}

FRigidBody* FRigidBody::GetParentBody() const
{
	if (Bone && Bone->Parent)
	{
		return Bone->Parent->Body;
	}

	return nullptr;
}

void FRigidBody::ApplyPushToRotateBody(const FVector& Push, const FVector& Offset)
{
	if (Pin && Pin->bEnabled && Pin->bPinRotation)
	{
		return; // rotation of this body is pinned
	}
	
	// equation 8 in "Detailed Rigid Body Simulation with XPBD"
	FVector Omega = InvMass * (1.0f - J.RotationStiffness) * FVector::CrossProduct(Offset, Push);
	FQuat OQ(Omega.X, Omega.Y, Omega.Z, 0.0f);
	ApplyRotationDelta(OQ, false);
}

void FRigidBody::ApplyPushToPosition(const FVector& Push)
{
	if (Pin && Pin->bEnabled)
	{
		return; // pins are locked
	}

	Position += Push * (1.0f - J.PositionStiffness);
}

void FRigidBody::ApplyRotationDelta(const FQuat& InDelta, const bool bNegated)
{
	if (Pin && Pin->bEnabled && Pin->bPinRotation)
	{
		return; // rotation of this body is pinned
	}

	/** InDelta is assumed to be a "pure" quaternion representing an infintesimal rotation */
	FQuat Delta = InDelta * Rotation;
	Delta.X *= 0.5f;
	Delta.Y *= 0.5f;
	Delta.Z *= 0.5f;
	Delta.W *= 0.5f;
	Rotation.X = Rotation.X + (bNegated ? -Delta.X : Delta.X);
	Rotation.Y = Rotation.Y + (bNegated ? -Delta.Y : Delta.Y);
	Rotation.Z = Rotation.Z + (bNegated ? -Delta.Z : Delta.Z);
	Rotation.W = Rotation.W + (bNegated ? -Delta.W : Delta.W);
	Rotation.Normalize();
}
} // namespace