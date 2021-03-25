// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_IKRig.h"
#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
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
	FPoseContext SourcePose(Output);

	if (Source.GetLinkNode())
	{
		Source.Evaluate(SourcePose);
	}
	else
	{
		SourcePose.ResetToRefPose();
	}

	if (!IKRigProcessor && IKRigProcessor->IsInitialized())
	{
		Output = SourcePose;
		return;
	}

	FCompactPose& OutPose = SourcePose.Pose;
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
	for (int32 GoalIndex = 0; GoalIndex < GoalNames.Num(); ++GoalIndex)
	{
		if (ensure(GoalTransforms.IsValidIndex(GoalIndex)))
		{
			IKRigProcessor->SetGoalTransform(
				GoalNames[GoalIndex], 
				GoalTransforms[GoalIndex].GetLocation(),
				GoalTransforms[GoalIndex].GetRotation());
		}
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

	Output.Pose.NormalizeRotations();

	// draw the interface
	QueueDrawInterface(Output.AnimInstanceProxy, Output.AnimInstanceProxy->GetComponentTransform());
}

void FAnimNode_IKRig::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	// alpha later?
	Source.GatherDebugData(DebugData.BranchFlow(1.f));
}

void FAnimNode_IKRig::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
	if (!IKRigProcessor)
	{
		return;
	}
	
	IKRigProcessor->Initialize(RigDefinitionAsset);
	GoalNames.Reset();
	IKRigProcessor->GetGoalNames(GoalNames);
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

bool FAnimNode_IKRig::RebuildGoalList()
{
	if (!RigDefinitionAsset)
	{
		return false;
	}
	
	TArray<FName> SolverGoals;
	RigDefinitionAsset->GetGoalNamesFromSolvers(SolverGoals);
	const int32 GoalNum = SolverGoals.Num();
	if (GoalTransforms.Num() != GoalNum)
	{
		GoalTransforms.SetNum(GoalNum);
		return true;
	}

	return false;
}

FName FAnimNode_IKRig::GetGoalName(int32 Index) const
{
	if (RigDefinitionAsset)
	{
		TArray<FName> Names;
		RigDefinitionAsset->GetGoalNamesFromSolvers(Names);
		if (Names.IsValidIndex(Index))
		{
			return Names[Index];
		}
	}

	return FName(TEXT("Invalid"));
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

void FAnimNode_IKRig::QueueDrawInterface(FAnimInstanceProxy* AnimProxy, const FTransform& ComponentToWorld)
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