// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigSkeleton.h"

#include "IKRigDefinition.h"

void FIKRigSkeleton::Initialize(const FReferenceSkeleton& RefSkeleton, const TArray<FName>& InExcludedBones)
{
	// reset all containers
	Reset();
	
	// copy names and parent indices into local storage
	TArray<FMeshBoneInfo> RawBoneInfo = RefSkeleton.GetRawRefBoneInfo();
	for (int32 BoneIndex=0; BoneIndex<RefSkeleton.GetRawBoneNum(); ++BoneIndex)
	{
		BoneNames.Add(RawBoneInfo[BoneIndex].Name);
		ParentIndices.Add(RawBoneInfo[BoneIndex].ParentIndex);	
	}

	// copy all the poses out of the ref skeleton
	CopyPosesFromRefSkeleton(RefSkeleton);
}

void FIKRigSkeleton::Reset()
{
	BoneNames.Reset();
	ParentIndices.Reset();
	ExcludedBones.Reset();
	CurrentPoseGlobal.Reset();
	CurrentPoseLocal.Reset();
	RefPoseGlobal.Reset();
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

FName FIKRigSkeleton::GetBoneNameFromIndex(const int32 BoneIndex) const
{
	if (BoneNames.IsValidIndex(BoneIndex))
	{
		return BoneNames[BoneIndex];
	}

	return NAME_None;
}

int32 FIKRigSkeleton::GetParentIndex(const int32 BoneIndex) const
{
	if (!BoneNames.IsValidIndex(BoneIndex))
	{
		return INDEX_NONE;
	}

	return ParentIndices[BoneIndex];
}

int32 FIKRigSkeleton::GetParentIndexThatIsNotExcluded(const int32 BoneIndex) const
{
	// find first parent that is NOT excluded
	int32 ParentIndex = GetParentIndex(BoneIndex);
	while(ParentIndex != INDEX_NONE)
	{
		if (!IsBoneExcluded(ParentIndex))
		{
			break;
		}
		ParentIndex = GetParentIndex(ParentIndex);
	}
	
	return ParentIndex;
}

bool FIKRigSkeleton::CopyPosesFromRefSkeleton(const FReferenceSkeleton& RefSkeleton)
{
	// get a compacted local ref pose based on the stored names
	TArray<FTransform> CompactRefPoseLocal;
	for (const FName& BoneName : BoneNames)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Rig is running on a skeleton that is missing bone named, '%s'."), *BoneName.ToString());
			return false;
		}
		CompactRefPoseLocal.Add(RefSkeleton.GetRefBonePose()[BoneIndex]);
	}
	
	// copy local ref pose to global
	ConvertLocalPoseToGlobal(ParentIndices, CompactRefPoseLocal, RefPoseGlobal);

	// initialize current GLOBAL pose to reference pose
	CurrentPoseGlobal = RefPoseGlobal;

	// initialize current LOCAL pose
	UpdateAllLocalTransformFromGlobal();

	return true; // supplied ref skeleton had all the bones we are looking for
}

void FIKRigSkeleton::ConvertLocalPoseToGlobal(
	const TArray<int32>& InParentIndices,
	const TArray<FTransform>& InLocalPose,
	TArray<FTransform>& OutGlobalPose)
{
	check(InLocalPose.Num() == InParentIndices.Num());
	
	OutGlobalPose.Reset();
	for (int32 BoneIndex=0; BoneIndex<InLocalPose.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = InParentIndices[BoneIndex];
		if (ParentIndex == INDEX_NONE)
		{
			OutGlobalPose.Add(InLocalPose[BoneIndex]);
			continue; // root bone is always in local space 
		}

		const FTransform& ChildLocalTransform  = InLocalPose[BoneIndex];
		const FTransform& ParentGlobalTransform  = OutGlobalPose[ParentIndex];
		OutGlobalPose.Add(ChildLocalTransform * ParentGlobalTransform);
	}
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

bool FIKRigSkeleton::IsBoneInDirectLineage(const FName& Child, const FName& PotentialParent) const
{
	const int32 ChildIndex = GetBoneIndexFromName(Child);
	if (ChildIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 PotentialParentIndex = GetBoneIndexFromName(PotentialParent);
	if (PotentialParentIndex == INDEX_NONE)
	{
		return false;
	}

	int32 NextParentIndex = ChildIndex;
	while(NextParentIndex != INDEX_NONE)
	{
		if (NextParentIndex == PotentialParentIndex)
		{
			return true;
		}
		NextParentIndex = ParentIndices[NextParentIndex];
	}

	return false;
}

bool FIKRigSkeleton::IsBoneExcluded(const int32 BoneIndex) const
{
	if (ensure(BoneNames.IsValidIndex(BoneIndex)))
	{
		return ExcludedBones.Contains(BoneNames[BoneIndex]);	
	}
	
	return false;
}

void FIKRigSkeleton::NormalizeRotations(TArray<FTransform>& Transforms)
{
	for (FTransform& Transform : Transforms)
	{
		Transform.NormalizeRotation();
	}
}

void FIKRigSkeleton::GetChainsInList(const TArray<int32>& SelectedBones, TArray<FIKRigSkeletonChain>& OutChains) const
{	
	// no bones provided, return empty list
	if (SelectedBones.IsEmpty())
	{
		return;
	}

	// get parents of all the selected bones
	TSet<int32> SelectedParentIndices;
	for (const int32 SelectedBone : SelectedBones)
	{
		SelectedParentIndices.Add(GetParentIndex(SelectedBone));
	}
	
	// find all selected leaf nodes and make each one a chain
    for (const int32 SelectedBone : SelectedBones)
    {
    	// is this a leaf node in the selection?
    	if (SelectedParentIndices.Contains(SelectedBone))
    	{
    		continue;
    	}

    	// convert leaf nodes to chains
    	int32 ChainStart;
    	const int32 ChainEnd = SelectedBone;
    	int32 ParentIndex = SelectedBone;
    	while (true)
    	{
    		ChainStart = ParentIndex;
    		ParentIndex = GetParentIndex(ParentIndex);
    		if (ParentIndex == INDEX_NONE)
    		{
    			break;
    		}
    		if (!SelectedBones.Contains(ParentIndex))
    		{
    			break;
    		}
    	}

    	const FName StartBoneName = GetBoneNameFromIndex(ChainStart);
    	const FName EndBoneName = GetBoneNameFromIndex(ChainEnd);
    	OutChains.Emplace(StartBoneName, EndBoneName);	
    }
}
