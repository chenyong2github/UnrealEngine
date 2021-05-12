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

	if (!IKRigProcessor)
	{
		return;
	}
	
	if (!IKRigProcessor->IsInitialized())
	{
		return;
	}

	FCompactPose& OutPose = Output.Pose;
	FIKRigSkeleton& IKRigSkeleton = IKRigProcessor->GetSkeleton();
	
	if (bStartFromRefPose)
	{
		// start Solve() from REFERENCE pose
		IKRigProcessor->SetInputPoseToRefPose();
	}else
	{
		// start Solve() from INPUT pose

		// copy local bone transforms into IKRigProcessor skeleton
		for (FCompactPoseBoneIndex CPIndex : OutPose.ForEachBoneIndex())
		{
			int32* Index = CompactPoseToRigIndices.Find(CPIndex);
			if (Index)
			{
				IKRigSkeleton.CurrentPoseLocal[*Index] = OutPose[CPIndex];
			}
		}
		// update global pose in IK Rig
		IKRigSkeleton.UpdateAllGlobalTransformFromLocal();
	}

	// update goal transforms before solve
	for (const FIKRigGoal& Goal : Goals)
	{
		IKRigProcessor->SetIKGoal(Goal);
	}

	// goals from goal creator components will override any goals that were manually set (they take precedence)
	for (const TPair<FName, FIKRigGoal>& GoalPair : GoalsFromGoalCreators)
	{
		IKRigProcessor->SetIKGoal(GoalPair.Value);
	}

	// run stack of solvers, updates global transforms with new pose
	IKRigProcessor->Solve();

	// update local transforms of current IKRig pose
	IKRigSkeleton.UpdateAllLocalTransformFromGlobal();

	// copy local transforms to output pose
	for (FCompactPoseBoneIndex CPIndex : OutPose.ForEachBoneIndex())
	{
		int32* Index = CompactPoseToRigIndices.Find(CPIndex);
		if (Index)
		{
			Output.Pose[CPIndex] = IKRigSkeleton.CurrentPoseLocal[*Index];
		}
	}

	// draw the interface
	QueueDrawInterface(Output.AnimInstanceProxy, Output.AnimInstanceProxy->GetComponentTransform());
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
	
	// get the retargeted local ref pose to initialize the IK with
	const FReferenceSkeleton& RefSkeleton = Context.AnimInstanceProxy->GetSkelMeshComponent()->SkeletalMesh->GetRefSkeleton();
	// initialize the IK Rig
	IKRigProcessor->Initialize(RigDefinitionAsset, RefSkeleton);
}

void FAnimNode_IKRig::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	FAnimNode_Base::OnInitializeAnimInstance(InProxy, InAnimInstance);
	
	if (RigDefinitionAsset && !IKRigProcessor)
	{
		IKRigProcessor = UIKRigProcessor::MakeNewIKRigProcessor(InAnimInstance->GetOwningComponent());
	}
}

void FAnimNode_IKRig::PreUpdate(const UAnimInstance* InAnimInstance)
{
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
	
	const TArray<FIKRigEffectorGoal>& AllGoalNamesInIKRig = RigDefinitionAsset->GetEffectorGoals();
	const int32 NumGoalsInRig = AllGoalNamesInIKRig.Num();
	if (Goals.Num() != NumGoalsInRig)
	{
		Goals.SetNum(NumGoalsInRig);
		for (int32 i=0; i<NumGoalsInRig; ++i)
		{
			Goals[i].Name = AllGoalNamesInIKRig[i].Goal;
		}
		return true;
	}

	return false;
}

FName FAnimNode_IKRig::GetGoalName(int32 Index) const
{
	if (RigDefinitionAsset)
	{
		return RigDefinitionAsset->GetGoalName(Index);
	}

	return NAME_None;
}

void FAnimNode_IKRig::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);
	FAnimNode_Base::Update_AnyThread(Context);
	Source.Update(Context);
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
	check (IKRigProcessor);
	check (AnimProxy);

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
// 			MeshBuilder.Draw(PDI, InstructionTransform.ToMatrixWithScale(), Instruction.MaterialRenderProxy, SDPG_World/*SDPG_Foreground*/);
			break;
		}

		}
	}
}