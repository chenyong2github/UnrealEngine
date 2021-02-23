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
		SetGoal(Bone->Position, Bone->Rotation, 1.0f, 1.0f);
	}

	void FEffector::SetGoal(
		const FVector InPositionGoal,
		const FQuat& InRotationGoal,
		float InTransformAlpha,
		float InStrengthAlpha)
	{
		PositionOrig = Bone->Position;
		RotationOrig = Bone->Rotation;

		Position = PositionGoal = InPositionGoal;
		Rotation = RotationGoal = InRotationGoal;

		TransformAlpha = InTransformAlpha;
		StrengthAlpha = InStrengthAlpha;
	}

	void FEffector::UpdateFromInputs()
	{
		Position = FMath::Lerp(PositionOrig, PositionGoal, TransformAlpha);
		Rotation = FMath::Lerp(RotationOrig, RotationGoal, TransformAlpha);
		Pin.Pin()->GoalPoint = Position;
		Pin.Pin()->Alpha = StrengthAlpha;
	}

	void FEffector::SquashSubRoots()
	{
		// optionally apply a preferred angle to give solver a hint which direction to favor
		// apply amount of preferred angle proportional to the amount this sub-limb is squashed
		if (!ParentSubRoot || DistToSubRootOrig <= SMALL_NUMBER)
		{
			return;
		}

		// we have to be careful here when calculating the distance to the parent sub-root.
		// if the the parent sub-root is attached to an effector, use the effector's position
		// otherwise use the current position of the FRigidBody
		FEffector* ParentEffector = ParentSubRoot->AttachedEffector;
		const FVector ParentSubRootPosition = ParentEffector ? ParentEffector->Position : ParentSubRoot->Position;
		const float DistToNearestSubRoot = (ParentSubRootPosition - Position).Size();
		if (DistToNearestSubRoot >= DistToSubRootOrig)
		{
			return; // limb is stretched
		}

		// shrink distance to reach full blend to preferred angle
		const float ScaledDistOrig = DistToSubRootOrig * 0.3f;
		// amount squashed (clamped to scaled original length)
		float DeltaSquash = DistToSubRootOrig - DistToNearestSubRoot;
		DeltaSquash = DeltaSquash > ScaledDistOrig ? ScaledDistOrig : DeltaSquash;
		const float SquashPercent = DeltaSquash / ScaledDistOrig;
		if (SquashPercent < 0.01f)
		{
			return; // limb not squashed enough
		}

		FBone* Parent = Bone->Parent;
		while (Parent && Parent->bIsSolved)
		{
			if (Parent->Body->J.bUsePreferredAngles)
			{
				FQuat PartialRotation = FQuat::FastLerp(FQuat::Identity, FQuat(Parent->Body->J.PreferredAngles), SquashPercent);
				Parent->Body->Rotation = Parent->Body->Rotation * PartialRotation;
				Parent->Body->Rotation.Normalize();
			}

			if (Parent == ParentSubRoot->Bone)
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

	// initialize local bone transforms
	// this has to be done every tick because incoming animation can modify these
	// even the LocalPosition has to be updated incase translation is animated
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

	// update Bodies with new bone positions from incoming pose and solver settings
	for (FRigidBody& Body : Bodies)
	{
		Body.UpdateFromInputs(Settings);
	}

	// optionally pin root in-place (convenience, does not require an effector)
	if (RootPin.IsValid())
	{
		RootPin.Pin()->bEnabled = Settings.bPinRoot;
	}

	// blend effectors by Alpha and update pin goals
	for (FEffector& Effector : Effectors)
	{
		Effector.UpdateFromInputs();
	}

	// squash sub-roots to apply preferred angles
	for (FEffector& Effector : Effectors)
	{
		Effector.SquashSubRoots();
	}

	// run constraint iterations while allowing stretch, just to get reaching pose
	for (int32  I = 0; I < Settings.Iterations; ++I)
	{
		const bool bMoveSubRoots = true;
		for (auto Constraint : Constraints)
		{
			Constraint->Solve(bMoveSubRoots);
		}
	}

	if (!Settings.bAllowStretch)
	{
		for (int32 C = Constraints.Num() - 1; C >= 0; --C)
		{
			Constraints[C]->RemoveStretch();
		}

		for (FEffector& Effector : Effectors)
		{
			Effector.SquashSubRoots(); // update squashing once again
		}

		for (int32  I = 0; I < Settings.Iterations; ++I)
		{
			const bool bMoveSubRoots = false;
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
		if (Bone->bIsSolverRoot)
		{
			continue; // if there's an effector on the root, leave it where the body ended up
		}
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
		const bool bIndexInRange = Bone.ParentIndex >= 0 && Bone.ParentIndex < Bones.Num();
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
			FBone* BodyBone = NextBone->bIsSolverRoot ? NextBone : NextBone->Parent;
			if (!ensureMsgf(BodyBone, TEXT("PBIK: effector is on bone that is not on or below root bone.")))
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

	// initialize Effector's nearest ParentSubRoot (FBody) pointer
	// must be done AFTER setting: Bone.IsSubRoot/IsSolverRoot/Parent
	for (FEffector& Effector : Effectors)
	{
		FBone* Parent = Effector.Bone->Parent;
		while (Parent)
		{
			if (!Parent->bIsSolved)
			{
				break; // this only happens when effector is on solver root
			}

			if (Parent->bIsSubRoot || Parent->bIsSolverRoot)
			{
				Effector.ParentSubRoot = Parent->Body;
				Effector.DistToSubRootOrig = (Parent->Body->Position - Effector.Bone->Position).Size();
				break;
			}

			Parent = Parent->Parent;
		}
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
	using PBIK::FBone;
	using PBIK::FRigidBody;
	using PBIK::FPinConstraint;
	using PBIK::FJointConstraint;

	Constraints.Empty();

	// pin bodies to effectors
	for (FEffector& Effector : Effectors)
	{
		FBone* BodyBone = Effector.Bone->bIsSolverRoot ? Effector.Bone : Effector.Bone->Parent;
		if (!ensureMsgf(BodyBone, TEXT("PBIK: effector is on bone that does not have a parent.")))
		{
			return;
		}

		FRigidBody* Body = BodyBone->Body;
		TSharedPtr<FPinConstraint> Constraint = MakeShared<FPinConstraint>(Body, Effector.Position);
		Effector.Pin = Constraint;
		Body->AttachedEffector = &Effector;
		Constraints.Add(Constraint);
	}

	// pin root body to animated location 
	// this constraint is by default off in solver settings
	if (!SolverRoot->Body->AttachedEffector) // only add if user hasn't added their own root effector
	{
		const TSharedPtr<FPinConstraint> RootConstraint = MakeShared<FPinConstraint>(SolverRoot->Body, SolverRoot->Position);
		Constraints.Add(RootConstraint);
		RootPin = RootConstraint;
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

bool FPBIKSolver::IsReadyToSimulate() const
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

int32 FPBIKSolver::AddEffector(FName BoneName)
{
	for (PBIK::FBone& Bone : Bones)
	{
		if (Bone.Name == BoneName)
		{
			Effectors.Emplace(&Bone);
			return Effectors.Num() - 1;
		}
	}

	return -1;
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

int32 FPBIKSolver::GetBoneIndex(FName BoneName) const
{
	for (int32 B=0; B < Bones.Num(); ++B)
	{
		if (Bones[B].Name == BoneName)
		{
			return B;
		}
	}

	return INDEX_NONE;
}

void FPBIKSolver::SetEffectorGoal(
	const int32 Index,
	const FVector& InPosition,
	const FQuat& InRotation,
	const float OffsetAlpha,
	const float StrengthAlpha)
{
	check(Index >= 0 && Index < Effectors.Num());
	Effectors[Index].SetGoal(InPosition, InRotation, OffsetAlpha, StrengthAlpha);
}
