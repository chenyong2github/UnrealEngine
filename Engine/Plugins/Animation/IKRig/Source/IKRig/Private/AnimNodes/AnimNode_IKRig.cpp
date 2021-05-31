// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_IKRig.h"
#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "IKRigSolver.h"
#include "ActorComponents/IKRigInterface.h"
#include "Drawing/ControlRigDrawInterface.h"
#include "Animation/AnimInstanceProxy.h"


FAnimNode_IKRig::FAnimNode_IKRig()
	: RigDefinitionAsset (nullptr)
#if WITH_EDITORONLY_DATA
	, bEnableDebugDraw(false)
#endif
{
}

void FAnimNode_IKRig::Evaluate_AnyThread(FPoseContext& Output) 
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Source.GetLinkNode() && !bStartFromRefPose)
	{
		Source.Evaluate(Output);
	}
	else
	{
		Output.ResetToRefPose();
	}

	if (!RigDefinitionAsset)
	{
		return;
	}

	if (IKRigProcessor.NeedsInitialized(RigDefinitionAsset))
	{
		return;
	}

	// copy input pose to solver stack
	CopyInputPoseToSolver(Output.Pose);
	// update target goal transforms
	AssignGoalTargets();
	// run stack of solvers, 
	IKRigProcessor.Solve();
	// updates transforms with new pose
	CopyOutputPoseToAnimGraph(Output.Pose);
	
	// debug drawing
	QueueDrawInterface(Output.AnimInstanceProxy, Output.AnimInstanceProxy->GetComponentTransform());
}

void FAnimNode_IKRig::CopyInputPoseToSolver(FCompactPose& InputPose)
{
	// start Solve() from REFERENCE pose
	if (bStartFromRefPose)
	{
		IKRigProcessor.SetInputPoseToRefPose();
		return;
	}
	
	// start Solve() from INPUT pose
	// copy local bone transforms into IKRigProcessor skeleton
	FIKRigSkeleton& IKRigSkeleton = IKRigProcessor.GetSkeleton();
	for (FCompactPoseBoneIndex CPIndex : InputPose.ForEachBoneIndex())
	{
		int32* Index = CompactPoseToRigIndices.Find(CPIndex);
		if (Index)
		{
			IKRigSkeleton.CurrentPoseLocal[*Index] = InputPose[CPIndex];
		}
	}
	// update global pose in IK Rig
	IKRigSkeleton.UpdateAllGlobalTransformFromLocal();
}

void FAnimNode_IKRig::AssignGoalTargets()
{
	// update goal transforms before solve
	// these transforms can come from a few different sources, handled here...

	// use the goal transforms from the source asset itself
	// this is used to live preview results from the IK Rig editor
	if (bDriveWithSourceAsset)
	{
		IKRigProcessor.CopyAllInputsFromSourceAssetAtRuntime(RigDefinitionAsset);
		return;
	}
	
	// copy transforms from this anim node's goal pins from blueprint
	for (const FIKRigGoal& Goal : Goals)
	{
		IKRigProcessor.SetIKGoal(Goal);
	}

	// override any goals that were manually set with goals from goal creator components (they take precedence)
	for (const TPair<FName, FIKRigGoal>& GoalPair : GoalsFromGoalCreators)
	{
		IKRigProcessor.SetIKGoal(GoalPair.Value);
	}
}

void FAnimNode_IKRig::CopyOutputPoseToAnimGraph(FCompactPose& OutputPose)
{
	FIKRigSkeleton& IKRigSkeleton = IKRigProcessor.GetSkeleton();
	
	// update local transforms of current IKRig pose
	IKRigSkeleton.UpdateAllLocalTransformFromGlobal();

	// copy local transforms to output pose
	for (FCompactPoseBoneIndex CPIndex : OutputPose.ForEachBoneIndex())
	{
		int32* Index = CompactPoseToRigIndices.Find(CPIndex);
		if (Index)
		{
			OutputPose[CPIndex] = IKRigSkeleton.CurrentPoseLocal[*Index];
		}
	}
}

void FAnimNode_IKRig::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	DebugData.AddDebugItem(FString::Printf(TEXT("%s IK Rig evaluated with %d Goals."), *DebugData.GetNodeName(this), Goals.Num()));
		
	for (const TPair<FName, FIKRigGoal>& GoalPair : GoalsFromGoalCreators)
	{
		DebugData.AddDebugItem(FString::Printf(TEXT("Goal supplied by actor component: %s"), *GoalPair.Value.ToString()));
	}

	for (const FIKRigGoal& Goal : Goals)
	{
		if (GoalsFromGoalCreators.Contains(Goal.Name))
		{
			continue;
		}
		
		DebugData.AddDebugItem(FString::Printf(TEXT("Goal supplied by node pin: %s"), *Goal.ToString()));
	}
}

void FAnimNode_IKRig::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
}

void FAnimNode_IKRig::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);
	FAnimNode_Base::Update_AnyThread(Context);
	Source.Update(Context);
}

void FAnimNode_IKRig::PreUpdate(const UAnimInstance* InAnimInstance)
{
	if (IKRigProcessor.NeedsInitialized(RigDefinitionAsset))
	{
		// get the retargeted local ref pose to initialize the IK with
		const FReferenceSkeleton& RefSkeleton = InAnimInstance->CurrentSkeleton->GetReferenceSkeleton();
		// initialize the IK Rig (will only try once on the current version of the rig asset)
		IKRigProcessor.Initialize(RigDefinitionAsset, RefSkeleton, InAnimInstance->GetSkelMeshComponent());
	}
	
	// cache list of goal creator components on the actor
	// TODO tried doing this in Initialize_AnyThread but it would miss some GoalCreator components
	// so it was moved here to be more robust, but we need to profile this and make sure it's not hurting perf
	// (it may be enough to run this once and then never again...needs testing)
	GoalCreators.Reset();
	USkeletalMeshComponent* SkelMeshComponent = InAnimInstance->GetSkelMeshComponent();
	AActor* OwningActor = SkelMeshComponent->GetOwner();
	TArray<UActorComponent*> GoalCreatorComponents =  OwningActor->GetComponentsByInterface( UIKGoalCreatorInterface::StaticClass() );
	for (UActorComponent* GoalCreatorComponent : GoalCreatorComponents)
	{
		IIKGoalCreatorInterface* GoalCreator = Cast<IIKGoalCreatorInterface>(GoalCreatorComponent);
		if (!ensureMsgf(GoalCreator, TEXT("Goal creator component failed cast to IIKGoalCreatorInterface.")))
		{
			continue;
		}
		GoalCreators.Add(GoalCreator);
	}
	
	// pull all the goals out of any goal creators on the owning actor
	// this is done on the main thread because we're talking to actor components here
	GoalsFromGoalCreators.Reset();
	for (IIKGoalCreatorInterface* GoalCreator : GoalCreators)
	{
		GoalCreator->AddIKGoals_Implementation(GoalsFromGoalCreators);
	}
}

bool FAnimNode_IKRig::RebuildGoalList()
{
	if (!RigDefinitionAsset)
	{
		return false;
	}

	// number of goals changed
	const int32 NumGoalsInRig = RigDefinitionAsset->Goals.Num();
	if (Goals.Num() != NumGoalsInRig)
	{
		Goals.SetNum(NumGoalsInRig);
		for (int32 i=0; i<NumGoalsInRig; ++i)
		{
			Goals[i].Name = RigDefinitionAsset->Goals[i]->GoalName;
		}
		return true;
	}

	// potentially number of goals remains identical, but only names changed
	bool bNameUpdated = false;
	for (int32 i=0; i<NumGoalsInRig; ++i)
	{
		if (Goals[i].Name != RigDefinitionAsset->Goals[i]->GoalName)
		{
			Goals[i].Name = RigDefinitionAsset->Goals[i]->GoalName;
			bNameUpdated = true;
		}
	}

	return bNameUpdated;
}

FName FAnimNode_IKRig::GetGoalName(int32 Index) const
{
	if (RigDefinitionAsset && RigDefinitionAsset->Goals.IsValidIndex(Index))
	{
		return RigDefinitionAsset->Goals[Index]->GoalName;	
	}

	return NAME_None;
}

void FAnimNode_IKRig::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	FAnimNode_Base::CacheBones_AnyThread(Context);
	Source.CacheBones(Context);
	
	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
	if (!RequiredBones.IsValid())
	{
		return;
	}
	
	if (!RigDefinitionAsset)
	{
		return;
	}

	// fill up node names
	CompactPoseToRigIndices.Reset();
	const TArray<FBoneIndexType>& RequiredBonesArray = RequiredBones.GetBoneIndicesArray();
	const FReferenceSkeleton& RefSkeleton = RequiredBones.GetReferenceSkeleton();
	const int32 NumBones = RequiredBonesArray.Num();
	for (uint16 Index = 0; Index < NumBones; ++Index)
	{
		const int32 MeshBone = RequiredBonesArray[Index];
		if (!ensure(MeshBone != INDEX_NONE))
		{
			continue;
		}
		
		FCompactPoseBoneIndex CPIndex = RequiredBones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBone));
		const FName Name = RefSkeleton.GetBoneName(MeshBone);
		CompactPoseToRigIndices.Add(CPIndex) = RigDefinitionAsset->Skeleton.GetBoneIndexFromName(Name);
	}
}

void FAnimNode_IKRig::QueueDrawInterface(FAnimInstanceProxy* AnimProxy, const FTransform& ComponentToWorld) const
{
	check (AnimProxy);

	/*
	 *TODO implement basic drawing interface for IKRig solvers
	for (const FControlRigDrawInstruction& Instruction : IKRigProcessor->GetDrawInterface())
	{
		if (!Instruction.IsValid())
		{
			continue;
		}

		FTransform InstructionTransform = Instruction.Transform * ComponentToWorld;
		switch (Instruction.PrimitiveType)
		{
		case EControlRigDrawSettings::Points:
		{
			ensure(false);
			// no support for points yet, but feel free to add that to AnimProxy
// 			for (const FVector& Point : Instruction.Positions)
// 			{
// 				AnimProxy->Add (InstructionTransform.TransformPosition(Point), Instruction.Color, Instruction.Thickness, SDPG_Foreground);
// 			}
 			break;
		}
		case EControlRigDrawSettings::Lines:
		{
			const TArray<FVector>& Points = Instruction.Positions;
			for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex += 2)
			{
				AnimProxy->AnimDrawDebugLine(InstructionTransform.TransformPosition(Points[PointIndex]), InstructionTransform.TransformPosition(Points[PointIndex + 1]), Instruction.Color.ToFColor(false), false, 0.f, Instruction.Thickness);
			}
			break;
		}
		case EControlRigDrawSettings::LineStrip:
		{
			const TArray<FVector>& Points = Instruction.Positions;
			for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex++)
			{
				AnimProxy->AnimDrawDebugLine(InstructionTransform.TransformPosition(Points[PointIndex]), InstructionTransform.TransformPosition(Points[PointIndex + 1]), Instruction.Color.ToFColor(false), false, 0.f, Instruction.Thickness);
			}
			break;
		}

		case EControlRigDrawSettings::DynamicMesh:
		{
			ensure(false);
			// no support for this yet
// 			FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
// 			MeshBuilder.AddVertices(Instruction.MeshVerts);
// 			MeshBuilder.AddTriangles(Instruction.MeshIndices);
// 			MeshBuilder.Draw(PDI, InstructionTransform.ToMatrixWithScale(), Instruction.MaterialRenderProxy, SDPG_World);
			break;
		}

		}
	}*/
}