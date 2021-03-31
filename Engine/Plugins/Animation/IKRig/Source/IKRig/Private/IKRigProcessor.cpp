// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolver.h"


UIKRigProcessor* UIKRigProcessor::MakeNewIKRigProcessor(UObject* Outer)
{
	return NewObject<UIKRigProcessor>(Outer);
}

void UIKRigProcessor::Initialize(UIKRigDefinition* InRigDefinition)
{
	bInitialized = false;

	if (!ensureMsgf(InRigDefinition, TEXT("Trying to initialize IKRigProcessor with a null IKRigDefinition asset.")))
	{
		return;
	}

	// copy skeleton data from IKRigDefinition
	Skeleton = InRigDefinition->Skeleton; // trivial copy assignment operator for POD

	// initialize goal names based on solvers
	TArray<FIKRigEffectorGoal> GoalNames;
	InRigDefinition->GetGoalNamesFromSolvers(GoalNames);
	GoalContainer.InitializeGoalsFromNames(GoalNames);
	for (const FIKRigEffectorGoal& GoalName : GoalNames)
	{
		FGoalBone NewGoalBone;
		NewGoalBone.BoneName = GoalName.Goal;
		NewGoalBone.BoneIndex = Skeleton.GetBoneIndexFromName(GoalName.Bone);

		// validate that the skeleton we are trying to solve this goal on contains the bone the goal expects
		if (NewGoalBone.BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s has a Goal, '%s' that references an unknown bone, '%s'. Cannot evaluate."),
				*GetName(), *GoalName.Goal.ToString(), *GoalName.Bone.ToString());
			return;
		}

		// validate that there is not already a different goal, with the same name, that is using a different bone
		// (all goals with the same name must reference the same bone within a single IK Rig)
		if (const FGoalBone* Bone = GoalBones.Find(GoalName.Goal))
		{
			if (Bone->BoneName != NewGoalBone.BoneName)
			{
				UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s has a Goal, '%s' that references different bones in different solvers, '%s' and '%s'. Cannot evaluate."),
                *GetName(), *GoalName.Goal.ToString(), *Bone->BoneName.ToString(), *NewGoalBone.BoneName.ToString());
				return;
			}
		}
		
		GoalBones.Add(GoalName.Goal, NewGoalBone);
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
	
	DrawInterface.Reset();

	// blend goals towards input pose by alpha
	for (TPair<FName, FIKRigGoal>& GoalPair : GoalContainer.Goals)
	{
		FIKRigGoal& Goal = GoalPair.Value;
		const FGoalBone& GoalBone = GoalBones[Goal.Name];
		const FTransform& InputPoseBoneTransform = Skeleton.CurrentPoseGlobal[GoalBone.BoneIndex];
		Goal.Position = FMath::Lerp(
			InputPoseBoneTransform.GetTranslation(),
			Goal.Position,
			Goal.PositionAlpha);
		Goal.Rotation = FQuat::FastLerp(
			InputPoseBoneTransform.GetRotation(),
			Goal.Rotation.Quaternion(),
			Goal.RotationAlpha).Rotator();
	}

	// run all the solvers
	for (UIKRigSolver* Solver : Solvers)
	{
		Solver->Solve(Skeleton, GoalContainer, &DrawInterface);
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
