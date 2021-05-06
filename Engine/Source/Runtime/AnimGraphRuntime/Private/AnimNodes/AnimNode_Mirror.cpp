// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_Mirror.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "AnimationRuntime.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/MirrorSyncScope.h"

#define LOCTEXT_NAMESPACE "AnimNode_Mirror"

FAnimNode_Mirror::FAnimNode_Mirror()
	: MirrorDataTable(nullptr)
	, BlendTimeOnMirrorStateChange(0.0f)
	, bMirror(true)
	, bBoneMirroring(true)
	, bCurveMirroring(true)
	, bAttributeMirroring(true)
	, bResetChildOnMirrorStateChange(false)
	, bMirrorState(false)
	, bMirrorStateIsValid(false)
{
}

#if WITH_EDITOR
UMirrorDataTable* FAnimNode_Mirror::GetMirrorDataTable() const 
{ 
	return MirrorDataTable.Get();
}

void FAnimNode_Mirror::SetMirrorDataTable(UMirrorDataTable* MirrorTable)
{
	MirrorDataTable = MirrorTable; 
}

#endif 

void FAnimNode_Mirror::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
}

void FAnimNode_Mirror::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	Super::CacheBones_AnyThread(Context);

	if (MirrorDataTable && Context.AnimInstanceProxy)
	{
		const FBoneContainer& BoneContainer = Context.AnimInstanceProxy->GetRequiredBones();
		bool bSharesSkeleton = BoneContainer.GetSkeletonAsset() == MirrorDataTable->Skeleton;
		bool bIdenticalJointCount = BoneContainer.GetReferenceSkeleton().GetNum() == MirrorDataTable->BoneToMirrorBoneIndex.Num(); 

		if (bSharesSkeleton && bIdenticalJointCount)
		{
			MirrorDataTable->FillCompactPoseMirrorBones(BoneContainer, MirrorDataTable->BoneToMirrorBoneIndex, CompactPoseMirrorBones);
		}
		else
		{
			TArray<int32> MirrorBoneIndexes;
			MirrorDataTable->FillMirrorBoneIndexes(BoneContainer.GetReferenceSkeleton(), MirrorBoneIndexes);
			MirrorDataTable->FillCompactPoseMirrorBones(BoneContainer, MirrorBoneIndexes, CompactPoseMirrorBones);
		}

		const int32 NumBones = BoneContainer.GetCompactPoseNumBones();
		ComponentSpaceRefRotations.SetNumUninitialized(NumBones);
		ComponentSpaceRefRotations[FCompactPoseBoneIndex(0)] = BoneContainer.GetRefPoseTransform(FCompactPoseBoneIndex(0)).GetRotation();
		for (FCompactPoseBoneIndex BoneIndex(1); BoneIndex < NumBones; ++BoneIndex)
		{
			const FCompactPoseBoneIndex ParentBoneIndex = BoneContainer.GetParentBoneIndex(BoneIndex);
			ComponentSpaceRefRotations[BoneIndex] = ComponentSpaceRefRotations[ParentBoneIndex] * BoneContainer.GetRefPoseTransform(BoneIndex).GetRotation();
		}
	}
	else
	{
		CompactPoseMirrorBones.Reset();
		ComponentSpaceRefRotations.Reset(); 
	}
}

void FAnimNode_Mirror::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);

	if (bMirrorStateIsValid && bMirrorState != bMirror)
	{
		if (BlendTimeOnMirrorStateChange > SMALL_NUMBER)
		{
			// Inertialize when switching between mirrored and unmirrored states to smooth out the pose discontinuity
			UE::Anim::IInertializationRequester* InertializationRequester = Context.GetMessage<UE::Anim::IInertializationRequester>();
			if (InertializationRequester)
			{
				InertializationRequester->RequestInertialization(BlendTimeOnMirrorStateChange);
				InertializationRequester->AddDebugRecord(*Context.AnimInstanceProxy, Context.GetCurrentNodeId());
			}
			else
			{
				FAnimNode_Inertialization::LogRequestError(Context, Source);
			}
		}

		// Optionally reinitialize the source when the mirror state changes
		if (bResetChildOnMirrorStateChange)
		{
			FAnimationInitializeContext ReinitializeContext(Context.AnimInstanceProxy, Context.SharedContext);
			Source.Initialize(ReinitializeContext);
		}
	}

	UE::Anim::TOptionalScopedGraphMessage<UE::Anim::FMirrorSyncScope> Message(bMirror, Context, Context, MirrorDataTable);
	
	bMirrorState = bMirror;
	bMirrorStateIsValid = true;

	Source.Update(Context);
}

void FAnimNode_Mirror::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);

	if (bMirrorState && MirrorDataTable)
	{
		if (bBoneMirroring)
		{
			FAnimationRuntime::MirrorPose(Output.Pose, MirrorDataTable->MirrorAxis, CompactPoseMirrorBones, ComponentSpaceRefRotations);
		}

		if (bCurveMirroring)
		{
			FAnimationRuntime::MirrorCurves(Output.Curve, *MirrorDataTable);
		}

		if (bAttributeMirroring)
		{
			UE::Anim::Attributes::MirrorAttributes(Output.CustomAttributes, *MirrorDataTable);
		}
	}
}

void FAnimNode_Mirror::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("(Mirrored: %s)"), (bMirrorState) ? TEXT("true") : TEXT("false"));
	DebugData.AddDebugItem(DebugLine);

	Source.GatherDebugData(DebugData);
}


#undef LOCTEXT_NAMESPACE
