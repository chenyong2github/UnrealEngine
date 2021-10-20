// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolver.h"

void UIKRigProcessor::Initialize(UIKRigDefinition* InRigAsset, const FIKRigInputSkeleton& InputSkeleton)
{
	// we instantiate UObjects here which MUST be done on game thread...
	check(IsInGameThread());
	check(InRigAsset);
	
	bInitialized = false;

	// bail out if we've already tried initializing with this exact version of the rig asset
	if (bTriedToInitialize)
	{
		return; // don't keep spamming
	}

	// ok, lets try to initialize
	bTriedToInitialize = true;
	
	if (InRigAsset->Skeleton.BoneNames.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Trying to initialize IKRigProcessor with a IKRigDefinition that has no skeleton: %s"), *InRigAsset->GetName());
		return;
	}

	if (InRigAsset->GetSolverArray().IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Trying to initialize IKRigProcessor with a IKRigDefinition that has no solvers: %s"), *InRigAsset->GetName());
		return;
	}

	if (InRigAsset->GetGoalArray().IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Trying to initialize IKRigProcessor with a IKRigDefinition that has no goals: %s"), *InRigAsset->GetName());
		return;
	}

	// copy skeleton data from IKRigDefinition
	// we use the serialized bone names and parent indices (from when asset was initialized)
	// but we use the CURRENT ref pose from the currently running skeletal mesh (RefSkeleton)
	Skeleton.BoneNames = InRigAsset->Skeleton.BoneNames;
	Skeleton.ParentIndices = InRigAsset->Skeleton.ParentIndices;
	Skeleton.ExcludedBones = InRigAsset->Skeleton.ExcludedBones;
	const bool bSkeletonIsCompatible = Skeleton.CopyPosesFromInputSkeleton(InputSkeleton);
	if (!bSkeletonIsCompatible)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Rig, %s trying to run on a skeleton that does not have the required bones."), *InRigAsset->GetName());
		return;
	}
	
	// initialize goals based on source asset
	GoalContainer.Empty();
	const TArray<UIKRigEffectorGoal*>& GoalsInAsset = InRigAsset->GetGoalArray();
	for (const UIKRigEffectorGoal* GoalInAsset : GoalsInAsset)
	{
		// add a copy of the Goal to the container
		GoalContainer.SetIKGoal(GoalInAsset);
	}
	
	// initialize goal bones from asset
	GoalBones.Reset();
	for (const UIKRigEffectorGoal* EffectorGoal : GoalsInAsset)
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
	const TArray<UIKRigSolver*>& AssetSolvers = InRigAsset->GetSolverArray();
	Solvers.Reset(AssetSolvers.Num());
	int32 SolverIndex = 0;
	for (const UIKRigSolver* IKRigSolver : AssetSolvers)
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
		UIKRigSolver* Solver = DuplicateObject(IKRigSolver, this, FName(*Name));
		Solver->Initialize(Skeleton);
		Solvers.Add(Solver);
	}

	bInitialized = true;
}

void UIKRigProcessor::Initialize(UIKRigDefinition* InRigAsset, const FReferenceSkeleton& RefSkeleton)
{
	FIKRigInputSkeleton InputSkeleton;
	InputSkeleton.InitializeFromRefSkeleton(RefSkeleton);
	Initialize(InRigAsset, InputSkeleton);
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

void UIKRigProcessor::SetIKGoal(const UIKRigEffectorGoal* InGoal)
{
	check(bInitialized);
	GoalContainer.SetIKGoal(InGoal);
}

void UIKRigProcessor::Solve(const FTransform& ComponentToWorld)
{
	check(bInitialized);
	
	// convert goals into component space and blend towards input pose by alpha
	ResolveFinalGoalTransforms(ComponentToWorld);

	// run all the solvers
	for (UIKRigSolver* Solver : Solvers)
	{
		#if WITH_EDITOR
		if (Solver->IsEnabled())
		{
			Solver->Solve(Skeleton, GoalContainer);
		}
		#else
		Solver->Solve(Skeleton, GoalContainer);
		#endif
	}

	// make sure rotations are normalized coming out
	Skeleton.NormalizeRotations(Skeleton.CurrentPoseGlobal);
}

void UIKRigProcessor::CopyOutputGlobalPoseToArray(TArray<FTransform>& OutputPoseGlobal) const
{
	OutputPoseGlobal = Skeleton.CurrentPoseGlobal;
}

void UIKRigProcessor::Reset()
{
	Solvers.Reset();
	GoalContainer.Empty();
	GoalBones.Reset();
	Skeleton.Reset();
	SetNeedsInitialized();
}

void UIKRigProcessor::SetNeedsInitialized()
{
	bInitialized = false;
	bTriedToInitialize = false;
};

#if WITH_EDITOR

void UIKRigProcessor::CopyAllInputsFromSourceAssetAtRuntime(UIKRigDefinition* SourceAsset)
{
	check(SourceAsset)
	
	// copy goal settings
	const TArray<UIKRigEffectorGoal*>& AssetGoals =  SourceAsset->GetGoalArray();
	for (const UIKRigEffectorGoal* AssetGoal : AssetGoals)
	{
		SetIKGoal(AssetGoal);
	}

	// copy solver settings
	const TArray<UIKRigSolver*>& AssetSolvers = SourceAsset->GetSolverArray();
	check(Solvers.Num() == AssetSolvers.Num()); // if number of solvers has been changed, processor should have been reinitialized
	for (int32 SolverIndex=0; SolverIndex<Solvers.Num(); ++SolverIndex)
	{
		Solvers[SolverIndex]->SetEnabled(AssetSolvers[SolverIndex]->IsEnabled());
		Solvers[SolverIndex]->UpdateSolverSettings(AssetSolvers[SolverIndex]);
	}
}

#endif

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

void UIKRigProcessor::ResolveFinalGoalTransforms(const FTransform& WorldToComponent)
{
	for (FIKRigGoal& Goal : GoalContainer.Goals)
	{
		if (!GoalBones.Contains(Goal.Name))
		{
			// user is changing goals after initialization
			// not necessarily a bad thing, but new goal names won't work until re-init
			continue;
		}

		const FGoalBone& GoalBone = GoalBones[Goal.Name];
		const FTransform& InputPoseBoneTransform = Skeleton.CurrentPoseGlobal[GoalBone.BoneIndex];

		FVector ComponentSpaceGoalPosition = Goal.Position;
		FQuat ComponentSpaceGoalRotation = Goal.Rotation.Quaternion();

		// put goal POSITION in Component Space
		switch (Goal.PositionSpace)
		{
		case EIKRigGoalSpace::Additive:
			// add position offset to bone position
			ComponentSpaceGoalPosition = Skeleton.CurrentPoseGlobal[GoalBone.BoneIndex].GetLocation() + Goal.Position;
			break;
		case EIKRigGoalSpace::Component:
			// was already supplied in Component Space
			break;
		case EIKRigGoalSpace::World:
			// convert from World Space to Component Space
			ComponentSpaceGoalPosition = WorldToComponent.TransformPosition(Goal.Position);
			break;
		default:
			checkNoEntry();
			break;
		}
		
		// put goal ROTATION in Component Space
		switch (Goal.RotationSpace)
		{
		case EIKRigGoalSpace::Additive:
			// add rotation offset to bone rotation
			ComponentSpaceGoalRotation = Goal.Rotation.Quaternion() * Skeleton.CurrentPoseGlobal[GoalBone.BoneIndex].GetRotation();
			break;
		case EIKRigGoalSpace::Component:
			// was already supplied in Component Space
			break;
		case EIKRigGoalSpace::World:
			// convert from World Space to Component Space
			ComponentSpaceGoalRotation = WorldToComponent.TransformRotation(Goal.Rotation.Quaternion());
			break;
		default:
			checkNoEntry();
			break;
		}

		// blend by alpha from the input pose, to the supplied goal transform
		// when Alpha is 0, the goal transform matches the bone transform at the input pose.
		// when Alpha is 1, the goal transform is left fully intact
		Goal.FinalBlendedPosition = FMath::Lerp(
            InputPoseBoneTransform.GetTranslation(),
            ComponentSpaceGoalPosition,
            Goal.PositionAlpha);
		
		Goal.FinalBlendedRotation = FQuat::FastLerp(
            InputPoseBoneTransform.GetRotation(),
            ComponentSpaceGoalRotation,
            Goal.RotationAlpha);
	}
}
