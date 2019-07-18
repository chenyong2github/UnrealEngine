// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_CCDIK.h"
#include "Units/RigUnitContext.h"

void FRigUnit_CCDIK::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigHierarchy* Hierarchy = (FRigHierarchy*)(Context.HierarchyReference.Get());
	if (Hierarchy == nullptr)
	{
		return;
	}

	if (Context.State == EControlRigState::Init ||
		RotationLimits.Num() != RotationLimitIndex.Num())
	{
		BoneIndices.Reset();
		RotationLimitIndex.Reset();
		RotationLimitsPerBone.Reset();

		// verify the chain
		const int32 RootIndex = Hierarchy->GetIndex(StartBone);
		if (RootIndex != INDEX_NONE)
		{
			int32 CurrentIndex = EffectorIndex = Hierarchy->GetIndex(EffectorBone);
			while (CurrentIndex != INDEX_NONE)
			{
				// ensure the chain
				int32 ParentIndex = Hierarchy->GetParentIndex(CurrentIndex);
				if (ParentIndex != INDEX_NONE)
				{
					BoneIndices.Add(CurrentIndex);
				}

				if (ParentIndex == RootIndex)
				{
					BoneIndices.Add(RootIndex);
					break;
				}

				CurrentIndex = ParentIndex;
			}

			Chain.Reserve(BoneIndices.Num());
		}

		int32 RootParentIndex = Hierarchy->GetParentIndex(RootIndex);
		if (RootParentIndex != INDEX_NONE)
		{
			BoneIndices.Add(RootParentIndex);
		}

		Algo::Reverse(BoneIndices);

		RotationLimitsPerBone.SetNumUninitialized(BoneIndices.Num());
		for(const FRigUnit_CCDIK_RotationLimit& RotationLimit : RotationLimits)
		{
			int32 BoneIndex = Hierarchy->GetIndex(RotationLimit.Bone);
			BoneIndex = BoneIndices.Find(BoneIndex);
			RotationLimitIndex.Add(BoneIndex);
		}
	}
	else  if (Context.State == EControlRigState::Update)
	{
		if (BoneIndices.Num() > 0)
		{
			// Gather chain links. These are non zero length bones.
			Chain.Reset();
			
			for (int32 ChainIndex = 0; ChainIndex < BoneIndices.Num(); ChainIndex++)
			{
				const FTransform& GlobalTransform = Hierarchy->GetGlobalTransform(BoneIndices[ChainIndex]);
				const FTransform& LocalTransform = Hierarchy->GetLocalTransform(BoneIndices[ChainIndex]);
				Chain.Add(CCDIKChainLink(GlobalTransform, LocalTransform, ChainIndex));
			}

			for (float& Limit : RotationLimitsPerBone)
			{
				Limit = BaseRotationLimit;
			}
			
			for (int32 LimitIndex = 0; LimitIndex < RotationLimitIndex.Num(); LimitIndex++)
			{
				if (RotationLimitIndex[LimitIndex] != INDEX_NONE)
				{
					RotationLimitsPerBone[RotationLimitIndex[LimitIndex]] = RotationLimits[LimitIndex].Limit;
				}
			}

			bool bBoneLocationUpdated = AnimationCore::SolveCCDIK(Chain, EffectorTransform.GetLocation(), Precision, MaxIterations, bStartFromTail, RotationLimits.Num() > 0, RotationLimitsPerBone);

			// If we moved some bones, update bone transforms.
			if (bBoneLocationUpdated)
			{
				for (int32 LinkIndex = 0; LinkIndex < BoneIndices.Num(); LinkIndex++)
				{
					const CCDIKChainLink& CurrentLink = Chain[LinkIndex];
					Hierarchy->SetGlobalTransform(BoneIndices[LinkIndex], CurrentLink.Transform, bPropagateToChildren);
				}

				Hierarchy->SetGlobalTransform(EffectorIndex, EffectorTransform, bPropagateToChildren);
			}
		}
	}
}
