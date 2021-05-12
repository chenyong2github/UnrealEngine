// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolver.h"


UIKRigProcessor* UIKRigProcessor::MakeNewIKRigProcessor(UObject* Outer)
{
	return NewObject<UIKRigProcessor>(Outer);
}

void UIKRigProcessor::Initialize(UIKRigDefinition* InRigDefinition, const FReferenceSkeleton& RefSkeleton)
{
	bInitialized = false;

	if (!InRigDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("Trying to initialize IKRigProcessor with a null IKRigDefinition asset."));
		return;
	}

	// copy skeleton data from IKRigDefinition
	// we use the serialized bone names and parent indices (from when asset was initialized)
	// but we use the CURRENT ref pose from the currently running skeletal mesh (RefSkeleton)
	Skeleton.BoneNames = InRigDefinition->Skeleton.BoneNames;
	Skeleton.ParentIndices = InRigDefinition->Skeleton.ParentIndices;
	const bool bSkeletonIsCompatible = Skeleton.CopyPosesFromRefSkeleton(RefSkeleton);
	if (!bSkeletonIsCompatible)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s trying to run on a skeleton that does not have the required bones."), *GetName());
		return;
	}
	
	// initialize goal names based on solvers
	TArray<FIKRigEffectorGoal>& EffectorGoals = InRigDefinition->GetEffectorGoals();
	GoalContainer.InitializeGoalsFromNames(EffectorGoals);
	for (const FIKRigEffectorGoal& EffectorGoal : EffectorGoals)
	{
		FGoalBone NewGoalBone;
		NewGoalBone.BoneName = EffectorGoal.Bone;
		NewGoalBone.BoneIndex = Skeleton.GetBoneIndexFromName(EffectorGoal.Bone);

		// validate that the skeleton we are trying to solve this goal on contains the bone the goal expects
		if (NewGoalBone.BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s has a Goal, '%s' that references an unknown bone, '%s'. Cannot evaluate."),
				*GetName(), *EffectorGoal.Goal.ToString(), *EffectorGoal.Bone.ToString());
			return;
		}

		// validate that there is not already a different goal, with the same name, that is using a different bone
		// (all goals with the same name must reference the same bone within a single IK Rig)
		if (const FGoalBone* Bone = GoalBones.Find(EffectorGoal.Goal))
		{
			if (Bone->BoneName != NewGoalBone.BoneName)
			{
				UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s has a Goal, '%s' that references different bones in different solvers, '%s' and '%s'. Cannot evaluate."),
                *GetName(), *EffectorGoal.Goal.ToString(), *Bone->BoneName.ToString(), *NewGoalBone.BoneName.ToString());
				return;
			}
		}
		
		GoalBones.Add(EffectorGoal.Goal, NewGoalBone);
	}

	// create copies of all the solvers in the IK rig
	Solvers.Reset(InRigDefinition->Solvers.Num());
	for (UIKRigSolver* IKRigSolver : InRigDefinition->Solvers)
	{
		if (!IKRigSolver)
		{
			// this can happen if asset references deleted IK Solver type
			// which should only happen during development (if at all)
			UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s has null/unknown solver in it. Please remove it."), *GetName());
			continue;
		}
		
		UIKRigSolver* Solver = DuplicateObject(IKRigSolver, this);
		Solver->Initialize(Skeleton);
		Solvers.Add(Solver);
	}

	bInitialized = true;
}

void UIKRigProcessor::SetInputPoseGlobal(const TArray<FTransform>& InGlobalBoneTransforms) 
{
	check(bInitialized);
	check(InGlobalBoneTransforms.Num() == Skeleton.CurrentPoseGlobal.Num());
	Skeleton.CurrentPoseGlobal = InGlobalBoneTransforms;
	Skeleton.UpdateAllLocalTransformFromGlobal();
}

void UIKRigProcessor::SetInputPoseToRefPose()
{
	check(bInitialized);
	Skeleton.CurrentPoseGlobal = Skeleton.RefPoseGlobal;
	Skeleton.UpdateAllLocalTransformFromGlobal();
}

void UIKRigProcessor::SetIKGoal(const FIKRigGoal& InGoal)
{
	check(bInitialized);
	GoalContainer.SetIKGoal(InGoal);
}

void UIKRigProcessor::Solve()
{
	check(bInitialized);

	// blend goals towards input pose by alpha
	BlendGoalsByAlpha();

	// run all the solvers
	DrawInterface.Reset();
	for (UIKRigSolver* Solver : Solvers)
	{
		if (Solver->bEnabled)
		{
			Solver->Solve(Skeleton, GoalContainer, &DrawInterface);
		}
	}

	// make sure rotations are normalized coming out
	Skeleton.NormalizeRotations(Skeleton.CurrentPoseGlobal);
}

void UIKRigProcessor::CopyOutputGlobalPoseToArray(TArray<FTransform>& OutputPoseGlobal) const
{
	OutputPoseGlobal = Skeleton.CurrentPoseGlobal;
}

const FIKRigGoalContainer& UIKRigProcessor::GetGoalContainer() const
{
	check(bInitialized);
	return GoalContainer;
}

FIKRigSkeleton& UIKRigProcessor::GetSkeleton()
{
	check(bInitialized);
	return Skeleton;
}

bool UIKRigProcessor::GetBoneForGoal(FName GoalName, FGoalBone& OutBone) const
{
	const FGoalBone* FoundBone = GoalBones.Find(GoalName);
	if (FoundBone)
	{
		OutBone = *FoundBone;
		return true;
	}
	return false;
}

void UIKRigProcessor::BlendGoalsByAlpha()
{
	for (TPair<FName, FIKRigGoal>& GoalPair : GoalContainer.Goals)
	{
		if (!GoalBones.Contains(GoalPair.Key))
		{
			// user is changing goals after initialization
			// not necessarily a bad thing, but new goal names won't work until re-init
			continue;
		}
		
		FIKRigGoal& Goal = GoalPair.Value;
		const FGoalBone& GoalBone = GoalBones[Goal.Name];
		const FTransform& InputPoseBoneTransform = Skeleton.CurrentPoseGlobal[GoalBone.BoneIndex];
		
		Goal.FinalBlendedPosition = FMath::Lerp(
            InputPoseBoneTransform.GetTranslation(),
            Goal.Position,
            Goal.PositionAlpha);
		
		Goal.FinalBlendedRotation = FQuat::FastLerp(
            InputPoseBoneTransform.GetRotation(),
            Goal.Rotation.Quaternion(),
            Goal.RotationAlpha);
	}
}
