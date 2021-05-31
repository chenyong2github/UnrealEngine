// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolver.h"


void FIKRigProcessor::Initialize(UIKRigDefinition* InRigAsset, const FReferenceSkeleton& RefSkeleton, UObject* Outer)
{
	// we instantiate UObjects here which MUST be done on game thread...
	check(IsInGameThread());
	
	bInitialized = false;
	InitializedWithIKRigAssetVersion = -1;

	if (!InRigAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("Trying to initialize IKRigProcessor with a null IKRigDefinition asset."));
		return;
	}

	// bail out if we've already tried initializing with this exact version of the rig asset
	if (LastVersionTried == InRigAsset->GetAssetVersion())
	{
		return; // don't keep spamming
	}
	LastVersionTried = InRigAsset->GetAssetVersion();

	// copy skeleton data from IKRigDefinition
	// we use the serialized bone names and parent indices (from when asset was initialized)
	// but we use the CURRENT ref pose from the currently running skeletal mesh (RefSkeleton)
	Skeleton.BoneNames = InRigAsset->Skeleton.BoneNames;
	Skeleton.ParentIndices = InRigAsset->Skeleton.ParentIndices;
	const bool bSkeletonIsCompatible = Skeleton.CopyPosesFromRefSkeleton(RefSkeleton);
	if (!bSkeletonIsCompatible)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s trying to run on a skeleton that does not have the required bones."), *InRigAsset->GetName());
		return;
	}
	
	// initialize goal names based on solvers
	GoalContainer.InitializeFromGoals(InRigAsset->Goals);
	for (const UIKRigEffectorGoal* EffectorGoal : InRigAsset->Goals)
	{
		FGoalBone NewGoalBone;
		NewGoalBone.BoneName = EffectorGoal->BoneName;
		NewGoalBone.BoneIndex = Skeleton.GetBoneIndexFromName(EffectorGoal->BoneName);

		// validate that the skeleton we are trying to solve this goal on contains the bone the goal expects
		if (NewGoalBone.BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s has a Goal, '%s' that references an unknown bone, '%s'. Cannot evaluate."),
				*InRigAsset->GetName(), *EffectorGoal->GoalName.ToString(), *EffectorGoal->BoneName.ToString());
			return;
		}

		// validate that there is not already a different goal, with the same name, that is using a different bone
		// (all goals with the same name must reference the same bone within a single IK Rig)
		if (const FGoalBone* Bone = GoalBones.Find(EffectorGoal->GoalName))
		{
			if (Bone->BoneName != NewGoalBone.BoneName)
			{
				UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s has a Goal, '%s' that references different bones in different solvers, '%s' and '%s'. Cannot evaluate."),
                *InRigAsset->GetName(), *EffectorGoal->GoalName.ToString(), *Bone->BoneName.ToString(), *NewGoalBone.BoneName.ToString());
				return;
			}
		}
		
		GoalBones.Add(EffectorGoal->GoalName, NewGoalBone);
	}

	// create copies of all the solvers in the IK rig
	Solvers.Reset(InRigAsset->Solvers.Num());
	int32 SolverIndex = 0;
	for (UIKRigSolver* IKRigSolver : InRigAsset->Solvers)
	{
		if (!IKRigSolver)
		{
			// this can happen if asset references deleted IK Solver type
			// which should only happen during development (if at all)
			UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s has null/unknown solver in it. Please remove it."), *InRigAsset->GetName());
			continue;
		}

		// new solver name
		FString Name = IKRigSolver->GetName() + "_SolverInstance_";
		Name.AppendInt(SolverIndex++);
		UIKRigSolver* Solver = DuplicateObject(IKRigSolver, Outer, FName(*Name));
		Solver->AddToRoot();
		Solver->Initialize(Skeleton);
		Solvers.Add(Solver);
	}

	InitializedWithIKRigAssetVersion = InRigAsset->GetAssetVersion();
	bInitialized = true;
}

void FIKRigProcessor::SetInputPoseGlobal(const TArray<FTransform>& InGlobalBoneTransforms) 
{
	check(bInitialized);
	check(InGlobalBoneTransforms.Num() == Skeleton.CurrentPoseGlobal.Num());
	Skeleton.CurrentPoseGlobal = InGlobalBoneTransforms;
	Skeleton.UpdateAllLocalTransformFromGlobal();
}

void FIKRigProcessor::SetInputPoseToRefPose()
{
	check(bInitialized);
	Skeleton.CurrentPoseGlobal = Skeleton.RefPoseGlobal;
	Skeleton.UpdateAllLocalTransformFromGlobal();
}

void FIKRigProcessor::SetIKGoal(const FIKRigGoal& InGoal)
{
	check(bInitialized);
	GoalContainer.SetIKGoal(InGoal);
}

void FIKRigProcessor::SetIKGoal(const UIKRigEffectorGoal* InGoal)
{
	check(bInitialized);
	GoalContainer.SetIKGoal(InGoal);
}

void FIKRigProcessor::Solve()
{
	check(bInitialized);

	// blend goals towards input pose by alpha
	BlendGoalsByAlpha();

	// run all the solvers
	for (UIKRigSolver* Solver : Solvers)
	{
		if (Solver->bEnabled)
		{
			Solver->Solve(Skeleton, GoalContainer);
		}
	}

	// make sure rotations are normalized coming out
	Skeleton.NormalizeRotations(Skeleton.CurrentPoseGlobal);
}

void FIKRigProcessor::CopyOutputGlobalPoseToArray(TArray<FTransform>& OutputPoseGlobal) const
{
	OutputPoseGlobal = Skeleton.CurrentPoseGlobal;
}

void FIKRigProcessor::CopyAllInputsFromSourceAssetAtRuntime(UIKRigDefinition* IKRigAsset)
{
	// copy goal settings
	for (const UIKRigEffectorGoal* AssetGoal : IKRigAsset->Goals)
	{
		SetIKGoal(AssetGoal);
	}

	// copy solver settings
	check(Solvers.Num() == IKRigAsset->Solvers.Num()); // if number of solvers has been changed, processor should have been reinitialized
	for (int32 SolverIndex=0; SolverIndex<Solvers.Num(); ++SolverIndex)
	{
		Solvers[SolverIndex]->bEnabled = IKRigAsset->Solvers[SolverIndex]->bEnabled;
		Solvers[SolverIndex]->UpdateSolverSettings(IKRigAsset->Solvers[SolverIndex]);
	}
}

bool FIKRigProcessor::NeedsInitialized(UIKRigDefinition* IKRigAsset) const
{
	if (!bInitialized)
	{
		return true; // not initialized yet at all
	}

	if (!IKRigAsset)
	{
		return true; // lost connection to asset
	}

	if (InitializedWithIKRigAssetVersion != IKRigAsset->GetAssetVersion())
	{
		return true; // IKRig asset has been modified since last initialization
	}

	return false;
}

const FIKRigGoalContainer& FIKRigProcessor::GetGoalContainer() const
{
	check(bInitialized);
	return GoalContainer;
}

FIKRigSkeleton& FIKRigProcessor::GetSkeleton()
{
	check(bInitialized);
	return Skeleton;
}


void FIKRigProcessor::BlendGoalsByAlpha()
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
