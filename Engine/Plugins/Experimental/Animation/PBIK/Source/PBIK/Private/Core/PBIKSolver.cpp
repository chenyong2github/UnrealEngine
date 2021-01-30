// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/PBIKSolver.h"
#include "Core/PBIKBody.h"
#include "Core/PBIKConstraint.h"
#include "Core/PBIKDebug.h"

//#pragma optimize("", off)

namespace PBIK
{
	FEffector::FEffector(FBone* InBone)
	{
		check(InBone);
		Bone = InBone;
		SetGoal(Bone->Position, Bone->Rotation, 1.0f);
	}

	void FEffector::SetGoal(
		const FVector InPositionGoal,
		const FQuat& InRotationGoal,
		float InAlpha)
	{
		PositionOrig = Bone->Position;
		RotationOrig = Bone->Rotation;

		Position = PositionGoal = InPositionGoal;
		Rotation = RotationGoal = InRotationGoal;

		Alpha = InAlpha;
	}

	void FEffector::UpdateFromInputs()
	{
		Position = FMath::Lerp(PositionOrig, PositionGoal, Alpha);
		Rotation = FMath::Lerp(RotationOrig, RotationGoal, Alpha);
		Pin.Pin()->GoalPoint = Position;
		Pin.Pin()->Alpha = Alpha;
	}

	void FEffector::SquashSubRoots()
	{
		// optionally apply a preferred angle to give solver a hint which direction to favor
		// apply amount of preferred angle proportional to the amount this sub-limb is squashed
		if (!ParentSubRoot || DistToSubRootOrig <= SMALL_NUMBER)
		{
			return;
		}

		float DistToNearestSubRoot = (ParentSubRoot->Position - Position).Size();
		if (DistToNearestSubRoot >= DistToSubRootOrig)
		{
			return; // limb is stretched
		}

		// shrink distance to reach full blend to preferred angle
		float ScaledDistOrig = DistToSubRootOrig * 0.3f;
		// amount squashed (clamped to scaled original length)
		float DeltaSquash = DistToSubRootOrig - DistToNearestSubRoot;
		DeltaSquash = DeltaSquash > ScaledDistOrig ? ScaledDistOrig : DeltaSquash;
		float SquashPercent = DeltaSquash / ScaledDistOrig;
		if (SquashPercent < 0.01f)
		{
			return; // limb not squashed enough
		}

		FBone* Parent = Bone->Parent;
		while (Parent)
		{
			if (Parent->Body->J.bUsePreferredAngles)
			{
				FRotator Clamped = Parent->Body->J.PreferredAngles;
				FQuat PartialRotation = FQuat::FastLerp(FQuat::Identity, FQuat(Parent->Body->J.PreferredAngles), SquashPercent);
				Parent->Body->Rotation = Parent->Body->Rotation * PartialRotation;
				Parent->Body->Rotation.Normalize();
			}

			if (Parent == ParentSubRoot)
			{
				return;
			}

			Parent = Parent->Parent;
		}
	}

} // namespace

void FPBIKSolver::Solve(const FPBIKSolverSettings& Settings)
{
	SCOPE_CYCLE_COUNTER(STAT_PBIK_Solve);

	using PBIK::FEffector;
	using PBIK::FRigidBody;
	using PBIK::FBone;
	using PBIK::FBoneSettings;

	// don't run until properly initialized
	if (!Initialize())
	{
		return;
	}

	// update Bodies with new bone positions from incoming pose and solver settings
	for (FRigidBody& Body : Bodies)
	{
		Body.UpdateFromInputs(Settings);
	}

	// optionally pin root in-place
	RootPin.Pin()->bEnabled = Settings.bPinRoot;

	// blend effectors by Alpha and update pin goals
	for (FEffector& Effector : Effectors)
	{
		Effector.UpdateFromInputs();
		Effector.SquashSubRoots();
	}

	// run constraint iterations while allowing stretch, just to get reaching pose
	for (int32  I = 0; I < Settings.Iterations; ++I)
	{
		bool bMoveSubRoots = true;
		for (auto Constraint : Constraints)
		{
			Constraint->Solve(bMoveSubRoots);
		}
	}

	if (!Settings.bAllowStretch)
	{
		for (FEffector& Effector : Effectors)
		{
			Effector.SquashSubRoots(); // update squashing once again
		}

		for (int32  I = 0; I < Settings.Iterations; ++I)
		{
			bool bMoveSubRoots = false;
			for (auto Constraint : Constraints)
			{
				Constraint->Solve(bMoveSubRoots);
			}
		}

		for (int32  C = Constraints.Num() - 1; C >= 0; --C)
		{
			Constraints[C]->RemoveStretch();
		}
	}

	// update Bone transforms controlled by Bodies
	for (FRigidBody& Body : Bodies)
	{
		Body.Bone->Position = Body.Position + Body.Rotation * Body.BoneLocalPosition;
		Body.Bone->Rotation = Body.Rotation;
	}

	// update Bone transforms controlled by effectors
	for (const FEffector& Effector : Effectors)
	{
		FBone* Bone = Effector.Bone;
		Bone->Position = Bone->Parent->Position + Bone->Parent->Rotation * Bone->LocalPositionOrig;
		Bone->Rotation = Effector.Rotation;
	}

	// propagate to non-solved bones (requires storage in root to tip order)
	for (FBone& Bone : Bones)
	{
		if (Bone.bIsSolved || !Bone.Parent)
		{
			continue;
		}

		Bone.Position = Bone.Parent->Position + Bone.Parent->Rotation * Bone.LocalPositionOrig;
		Bone.Rotation = Bone.Parent->Rotation * Bone.LocalRotationOrig;
	}
}

bool FPBIKSolver::Initialize()
{
	if (bReadyToSimulate)
	{
		return true;
	}

	bReadyToSimulate = false;

	if (!InitBones())
	{
		return false;
	}

	if (!InitBodies())
	{
		return false;
	}

	InitConstraints();

	bReadyToSimulate = true;

	return true;
}

bool FPBIKSolver::InitBones()
{
	using PBIK::FEffector;
	using PBIK::FBone;

	if (!ensureMsgf(Bones.Num() > 0, TEXT("PBIK: no bones added to solver. Cannot initialize.")))
	{
		return false;
	}

	if (!ensureMsgf(Effectors.Num() > 0, TEXT("PBIK: no effectors added to solver. Cannot initialize.")))
	{
		return false;
	}

	// record solver root pointer
	int32  NumSolverRoots = 0;
	for (FBone& Bone : Bones)
	{
		if (Bone.bIsSolverRoot)
		{
			SolverRoot = &Bone;
			++NumSolverRoots;
		}
	}

	if (!ensureMsgf(SolverRoot, TEXT("PBIK: root bone not set. Cannot initialize.")))
	{
		return false;
	}

	if (!ensureMsgf(NumSolverRoots == 1, TEXT("PBIK: more than 1 bone was marked as solver root. Cannot initialize.")))
	{
		return false;
	}

	// initialize pointers to parents
	for (FBone& Bone : Bones)
	{
		// no parent on root, remains null
		if (Bone.ParentIndex == -1)
		{
			continue;
		}

		// validate parent
		bool bIndexInRange = Bone.ParentIndex >= 0 && Bone.ParentIndex < Bones.Num();
		if (!ensureMsgf(bIndexInRange,
			TEXT("PBIK: bone found with invalid parent index. Cannot initialize.")))
		{
			return false;
		}

		// record parent
		Bone.Parent = &Bones[Bone.ParentIndex];
	}

	// initialize IsSolved state by walking up from each effector to the root
	for (FEffector& Effector : Effectors)
	{
		FBone* NextBone = Effector.Bone;
		while (NextBone)
		{
			NextBone->bIsSolved = true;
			NextBone = NextBone->Parent;
			if (NextBone && NextBone->bIsSolverRoot)
			{
				NextBone->bIsSolved = true;
				break;
			}
		}
	}

	// initialize local bone transforms
	for (FBone& Bone : Bones)
	{
		FBone* Parent = Bone.Parent;
		if (!Parent)
		{
			continue;
		}

		Bone.LocalPositionOrig = Parent->Rotation.Inverse() * (Bone.Position - Parent->Position);
		Bone.LocalRotationOrig = Parent->Rotation.Inverse() * Bone.Rotation;
	}

	// initialize children lists
	for (FBone& Parent : Bones)
	{
		for (FBone& Child : Bones)
		{
			if (Child.bIsSolved && Child.Parent == &Parent)
			{
				Parent.Children.Add(&Child);
			}
		}
	}

	// initialize IsSubRoot flag
	for (FBone& Bone : Bones)
	{
		Bone.bIsSubRoot = Bone.Children.Num() > 1 || Bone.bIsSolverRoot;
	}

	// initialize Effector's nearest ParentSubRoot (FBone) pointer
	// must be done AFTER setting: Bone.IsSubRoot/IsSolverRoot/Parent
	for (FEffector& Effector : Effectors)
	{
		FBone* Parent = Effector.Bone->Parent;
		while (Parent)
		{
			if (Parent->bIsSubRoot || Parent->bIsSolverRoot)
			{
				Effector.ParentSubRoot = Parent;
				Effector.DistToSubRootOrig = (Parent->Position - Effector.Bone->Position).Size();
				break;
			}

			Parent = Parent->Parent;
		}
	}

	return true;
}

bool FPBIKSolver::InitBodies()
{
	using PBIK::FEffector;
	using PBIK::FRigidBody;
	using PBIK::FBone;

	Bodies.Empty();

	// create bodies
	for (FEffector& Effector : Effectors)
	{
		FBone* NextBone = Effector.Bone;
		while (true)
		{
			FBone* BodyBone = NextBone->Parent;
			if (!ensureMsgf(BodyBone, TEXT("PBIK: effector is on bone that is not child of root bone.")))
			{
				return false;
			}

			AddBodyForBone(BodyBone);

			NextBone = BodyBone;
			if (NextBone == SolverRoot)
			{
				break;
			}
		}
	}

	// initialize bodies
	for (FRigidBody& Body : Bodies)
	{
		Body.Initialize(SolverRoot);
	}

	// sort bodies root to leaf
	Bodies.Sort();
	Algo::Reverse(Bodies);

	// store pointers to bodies on bones (after sort!)
	for (FRigidBody& Body : Bodies)
	{
		Body.Bone->Body = &Body;
	}

	return true;
}

void FPBIKSolver::AddBodyForBone(PBIK::FBone* Bone)
{
	for (PBIK::FRigidBody& Body : Bodies)
	{
		if (Body.Bone == Bone)
		{
			return; // no duplicates
		}
	}
	Bodies.Emplace(Bone);
}

void FPBIKSolver::InitConstraints()
{
	using PBIK::FEffector;
	using PBIK::FRigidBody;
	using PBIK::FPinConstraint;
	using PBIK::FJointConstraint;

	Constraints.Empty();

	// pin root body to animated location (usually disabled by solver settings)
	TSharedPtr<FPinConstraint> RootConstraint = MakeShared<FPinConstraint>(SolverRoot->Body, SolverRoot->Position);
	Constraints.Add(RootConstraint);
	RootPin = RootConstraint;

	// pin bodies to effectors
	for (FEffector& Effector : Effectors)
	{
		if (!ensureMsgf(Effector.Bone->Parent, TEXT("PBIK: effector is on bone that does not have a parent.")))
		{
			return;
		}

		FRigidBody* Body = Effector.Bone->Parent->Body;
		TSharedPtr<FPinConstraint> Constraint = MakeShared<FPinConstraint>(Body, Effector.Position);
		Effector.Pin = Constraint;
		Body->bPinnedToEffector = true;
		Constraints.Add(Constraint);
	}

	// constrain all bodies together (child to parent)
	for (FRigidBody& Body : Bodies)
	{
		FRigidBody* ParentBody = Body.GetParentBody();
		if (!ParentBody)
		{
			continue; // root
		}

		TSharedPtr<FJointConstraint> Constraint = MakeShared<FJointConstraint>(ParentBody, &Body);
		Constraints.Add(Constraint);
	}
}

PBIK::FDebugDraw* FPBIKSolver::GetDebugDraw()
{
	DebugDraw.Solver = this;
	return &DebugDraw;
}

void FPBIKSolver::Reset()
{
	bReadyToSimulate = false;
	SolverRoot = nullptr;
	RootPin = nullptr;
	Bodies.Empty();
	Bones.Empty();
	Constraints.Empty();
	Effectors.Empty();
}

bool FPBIKSolver::IsReadyToSimulate()
{
	return bReadyToSimulate;
}

int32 FPBIKSolver::AddBone(
	const FName Name,
	const int32 ParentIndex,
	const FVector& InOrigPosition,
	const FQuat& InOrigRotation,
	bool bIsSolverRoot)
{
	return Bones.Emplace(Name, ParentIndex, InOrigPosition, InOrigRotation, bIsSolverRoot);
}

bool FPBIKSolver::AddEffector(FName BoneName)
{
	for (PBIK::FBone& Bone : Bones)
	{
		if (Bone.Name == BoneName)
		{
			Effectors.Emplace(&Bone);
			return true;
		}
	}

	return false;
}

void FPBIKSolver::SetBoneTransform(
	const int32 Index,
	const FTransform& InTransform)
{
	check(Index >= 0 && Index < Bones.Num());
	Bones[Index].Position = InTransform.GetLocation();
	Bones[Index].Rotation = InTransform.GetRotation();
}

PBIK::FBoneSettings* FPBIKSolver::GetBoneSettings(const int32 Index)
{
	// make sure to call Initialize() before applying bone settings
	if (!ensureMsgf(bReadyToSimulate, TEXT("PBIK: trying to access Bone Settings before Solver is initialized.")))
	{
		return nullptr;
	}

	if (!ensureMsgf(Bones.IsValidIndex(Index), TEXT("PBIK: trying to access Bone Settings with invalid bone index.")))
	{
		return nullptr;
	}

	if (!Bones[Index].Body)
	{
		UE_LOG(LogTemp, Warning, TEXT("PBIK: trying to apply Bone Settings to bone that is not simulated (not between root and effector)."));
		return nullptr;
	}

	return &Bones[Index].Body->J;
}

void FPBIKSolver::GetBoneGlobalTransform(const int32 Index, FTransform& OutTransform)
{
	check(Index >= 0 && Index < Bones.Num());
	PBIK::FBone& Bone = Bones[Index];
	OutTransform.SetLocation(Bone.Position);
	OutTransform.SetRotation(Bone.Rotation);
}

void FPBIKSolver::SetEffectorGoal(
	const int32 Index,
	const FVector& InPosition,
	const FQuat& InRotation,
	const float Alpha)
{
	check(Index >= 0 && Index < Effectors.Num());
	Effectors[Index].SetGoal(InPosition, InRotation, Alpha);
}
