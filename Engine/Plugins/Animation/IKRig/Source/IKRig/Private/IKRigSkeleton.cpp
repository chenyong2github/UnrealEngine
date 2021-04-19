// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigSkeleton.h"

void FIKRigSkeleton::Initialize(const FReferenceSkeleton& RefSkeleton)
{
	BoneNames.Reset();
	ParentIndices.Reset();
	CurrentPoseGlobal.Reset();
	CurrentPoseLocal.Reset();
	RefPoseGlobal.Reset();
	
	// copy names and parent indices into local storage
	for (int32 BoneIndex=0; BoneIndex<RefSkeleton.GetNum(); ++BoneIndex)
	{
		BoneNames.Add(RefSkeleton.GetBoneName(BoneIndex));
		ParentIndices.Add(RefSkeleton.GetParentIndex(BoneIndex));	
	}

	// store copy of reference pose (converted from local to global)
	const TArray<FTransform>& RefPoseLocal = RefSkeleton.GetRefBonePose();
	for (int32 BoneIndex=0; BoneIndex<RefPoseLocal.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = ParentIndices[BoneIndex];
		if (ParentIndex == INDEX_NONE)
		{
			RefPoseGlobal.Add(RefPoseLocal[BoneIndex]);
			continue; // root bone is always in local space 
		}

		const FTransform& ChildLocalTransform  = RefPoseLocal[BoneIndex];
		const FTransform& ParentGlobalTransform  = RefPoseGlobal[ParentIndex];
		RefPoseGlobal.Add(ChildLocalTransform * ParentGlobalTransform);
	}

	// initialize current GLOBAL pose to reference pose
	CurrentPoseGlobal = RefPoseGlobal;

	// initialize current LOCAL pose
	UpdateAllLocalTransformFromGlobal();
}

int32 FIKRigSkeleton::GetBoneIndexFromName(const FName InName) const
{
	for (int32 Index=0; Index<BoneNames.Num(); ++Index)
	{
		if (InName == BoneNames[Index])
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 FIKRigSkeleton::GetParentIndex(const int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex>ParentIndices.Num() || BoneIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return ParentIndices[BoneIndex];
}

void FIKRigSkeleton::UpdateAllGlobalTransformFromLocal()
{
	// generate GLOBAL transforms based on LOCAL transforms
	CurrentPoseGlobal = CurrentPoseLocal;
	for (int32 BoneIndex=0; BoneIndex<CurrentPoseLocal.Num(); ++BoneIndex)
	{
		UpdateGlobalTransformFromLocal(BoneIndex);
	}
}

void FIKRigSkeleton::UpdateGlobalTransformFromLocal(const int32 BoneIndex)
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		CurrentPoseGlobal[BoneIndex] = CurrentPoseLocal[BoneIndex]; // root bone is always in local space
		return; 
	}

	const FTransform& ChildLocalTransform  = CurrentPoseLocal[BoneIndex];
	const FTransform& ParentGlobalTransform  = CurrentPoseGlobal[ParentIndex];
	CurrentPoseGlobal[BoneIndex] = ChildLocalTransform * ParentGlobalTransform;
}

void FIKRigSkeleton::UpdateAllLocalTransformFromGlobal()
{
	// generate LOCAL transforms based on GLOBAL transforms
	CurrentPoseLocal = CurrentPoseGlobal;
	for (int32 BoneIndex=0; BoneIndex<CurrentPoseGlobal.Num(); ++BoneIndex)
	{
		UpdateLocalTransformFromGlobal(BoneIndex);
	}
}

void FIKRigSkeleton::UpdateLocalTransformFromGlobal(const int32 BoneIndex)
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		CurrentPoseLocal[BoneIndex] = CurrentPoseGlobal[BoneIndex]; // root bone is always in local space
		return; 
	}

	const FTransform& ChildGlobal  = CurrentPoseGlobal[BoneIndex];
	const FTransform& ParentGlobal  = CurrentPoseGlobal[ParentIndex];
	CurrentPoseLocal[BoneIndex] = ChildGlobal.GetRelativeTransform(ParentGlobal);
}

void FIKRigSkeleton::PropagateGlobalPoseBelowBone(const int32 StartBoneIndex)
{
	for (int32 BoneIndex=StartBoneIndex+1; BoneIndex<CurrentPoseGlobal.Num(); ++BoneIndex)
	{
		UpdateGlobalTransformFromLocal(BoneIndex);
		UpdateLocalTransformFromGlobal(BoneIndex);
	}
}

void FIKRigSkeleton::NormalizeRotations(TArray<FTransform>& Transforms)
{
	for (FTransform& Transform : Transforms)
	{
		Transform.NormalizeRotation();
	}
}