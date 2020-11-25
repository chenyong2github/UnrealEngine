// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_IKRig.h"
#include "IKRigProcessor.h"
#include "IKRigDefinition.h"
#include "Animation/AnimInstanceProxy.h"
/////////////////////////////////////////////////////
// AnimNode_IKRig

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
		// apply refpose
		SourcePose.ResetToRefPose();
	}

	if (RigProcessor)
	{
		FIKRigTransformModifier& TransformModifier = RigProcessor->GetIKRigTransformModifier();

		if (bStartFromRefPose)
		{
			RigProcessor->ResetToRefPose();
		}
		
		FCompactPose& OutPose = SourcePose.Pose;

		for (FCompactPoseBoneIndex CPIndex : OutPose.ForEachBoneIndex())
		{
			int32* Index = CompactPoseToRigIndices.Find(CPIndex);
			if (Index)
			{
				// Todo: this is again slow. 
				// the tricky part is that if we go start from child to parent
				// we may still missing joints that don't exists
				// so this is slow and for safety but needs revisit
				TransformModifier.SetLocalTransform(*Index, OutPose[CPIndex], true);
			}
		}

		for (int32 GoalIndex = 0; GoalIndex < GoalNames.Num(); ++GoalIndex)
		{
			if (ensure(GoalTransforms.IsValidIndex(GoalIndex)))
			{
				RigProcessor->SetGoalPosition( GoalNames[GoalIndex], GoalTransforms[GoalIndex].GetLocation());
				RigProcessor->SetGoalRotation(GoalNames[GoalIndex], GoalTransforms[GoalIndex].GetRotation().Rotator());
			}
		}

		// fill up current pose
		RigProcessor->Solve();

		// get component pose from control rig
//		FCSPose<FCompactPose> MeshPoses;
//		MeshPoses.InitPose(SourcePose.Pose);

		// now we copy back to output pose
		for (FCompactPoseBoneIndex CPIndex : OutPose.ForEachBoneIndex())
		{
			int32* Index = CompactPoseToRigIndices.Find(CPIndex);
			if (Index)
			{
//				MeshPoses.SetComponentSpaceTransform(CPIndex, TransformModifier.GetGlobalTransform(*Index));
				Output.Pose[CPIndex] = TransformModifier.GetLocalTransform(*Index);
			}
		}

//		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(MeshPoses, Output.Pose);
		Output.Pose.NormalizeRotations();
	}
	else
	{
		Output = SourcePose;
	}
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

	if (RigProcessor)
	{
		RigProcessor->SetIKRigDefinition(RigDefinitionAsset, true);
		GoalNames.Reset();
		RigProcessor->GetGoals(GoalNames);
	}
}

void FAnimNode_IKRig::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (RigDefinitionAsset && !RigProcessor)
	{
		RigProcessor = NewObject<UIKRigProcessor>(InAnimInstance->GetOwningComponent());
	}

	FAnimNode_Base::OnInitializeAnimInstance(InProxy, InAnimInstance);
}

void FAnimNode_IKRig::RebuildGoalList()
{
	if (RigDefinitionAsset)
	{
		GoalTransforms.SetNum(RigDefinitionAsset->GetGoals().Num());
	}
}

FName FAnimNode_IKRig::GetGoalName(int32 Index) const
{
	if (RigDefinitionAsset)
	{
		// TODO: fix  this vs RigProcessor goals. I think RigProcessor
		// only issue with RigProcessor is you don't know until it compiles
		TArray<FName> Names = RigDefinitionAsset->GetGoalsNames();
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

	if (RigProcessor)
	{
		// fill up node names
		const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();

		CompactPoseToRigIndices.Reset();

		if (RequiredBones.IsValid())
		{
			const TArray<FBoneIndexType>& RequiredBonesArray = RequiredBones.GetBoneIndicesArray();
			const int32 NumBones = RequiredBonesArray.Num();

			const FReferenceSkeleton& RefSkeleton = RequiredBones.GetReferenceSkeleton();
			const FIKRigHierarchy* Hierarchy = RigProcessor->GetHierarchy();

			if (Hierarchy)
			{

				// even if not mapped, we map only node that exists in the controlrig
				for (uint16 Index = 0; Index < NumBones; ++Index)
				{
					// get the name
					int32 MeshBone = RequiredBonesArray[Index];
					if (ensure(MeshBone != INDEX_NONE))
					{
						FCompactPoseBoneIndex CPIndex = RequiredBones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBone));
						FName Name = RequiredBones.GetReferenceSkeleton().GetBoneName(MeshBone);
						CompactPoseToRigIndices.Add(CPIndex) = Hierarchy->GetIndex(Name);
					}
				}
			}
		}
	}
}