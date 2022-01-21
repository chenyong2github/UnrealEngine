// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargeterController.h"

#include "ScopedTransaction.h"
#include "Algo/LevenshteinDistance.h"
#include "Engine/SkeletalMesh.h"
#include "Retargeter/IKRetargeter.h"

#define LOCTEXT_NAMESPACE "IKRetargeterController"


UIKRetargeterController* UIKRetargeterController::GetController(UIKRetargeter* InRetargeterAsset)
{
	if (!InRetargeterAsset)
	{
		return nullptr;
	}

	if (!InRetargeterAsset->Controller)
	{
		UIKRetargeterController* Controller = NewObject<UIKRetargeterController>();
		Controller->Asset = InRetargeterAsset;
		InRetargeterAsset->Controller = Controller;
	}

	UIKRetargeterController* Controller = Cast<UIKRetargeterController>(InRetargeterAsset->Controller);
	// clean the asset before editing
	Controller->CleanChainMapping();
	Controller->CleanPoseList();
	
	return Controller;
}

UIKRetargeter* UIKRetargeterController::GetAsset() const
{
	return Asset;
}

void UIKRetargeterController::SetSourceIKRig(UIKRigDefinition* SourceIKRig)
{
	Asset->SourceIKRigAsset = SourceIKRig;
}

void UIKRetargeterController::SetTargetIKRig(UIKRigDefinition* TargetIKRig)
{
	CleanChainMapping();
	AutoMapChains();
}

USkeletalMesh* UIKRetargeterController::GetTargetPreviewMesh()
{
	// can't preview anything if target IK Rig is null
	if (!Asset->GetTargetIKRig())
	{
		return nullptr;
	}

	// optionally prefer override if one is provided
	if (IsValid(Asset->TargetPreviewMesh))
	{
		return Asset->TargetPreviewMesh.Get();
	}

	// fallback to preview mesh from IK Rig asset
	return Asset->GetTargetIKRig()->PreviewSkeletalMesh.Get();
}

FName UIKRetargeterController::GetSourceRootBone() const
{
	return IsValid(Asset->SourceIKRigAsset) ? Asset->SourceIKRigAsset->GetRetargetRoot() : FName("None");
}

FName UIKRetargeterController::GetTargetRootBone() const
{
	return IsValid(Asset->TargetIKRigAsset) ? Asset->TargetIKRigAsset->GetRetargetRoot() : FName("None");
}

void UIKRetargeterController::GetTargetChainNames(TArray<FName>& OutNames) const
{
	if (IsValid(Asset->TargetIKRigAsset))
	{
		const TArray<FBoneChain>& Chains = Asset->TargetIKRigAsset->GetRetargetChains();
		for (const FBoneChain& Chain : Chains)
		{
			OutNames.Add(Chain.ChainName);
		}
	}
}

void UIKRetargeterController::GetSourceChainNames(TArray<FName>& OutNames) const
{
	if (IsValid(Asset->SourceIKRigAsset))
	{
		const TArray<FBoneChain>& Chains = Asset->SourceIKRigAsset->GetRetargetChains();
		for (const FBoneChain& Chain : Chains)
		{
			OutNames.Add(Chain.ChainName);
		}
	}
}

void UIKRetargeterController::CleanChainMapping()
{
	if (!IsValid(Asset->TargetIKRigAsset))
	{
		// empty the chain mapping
		Asset->ChainMapping.Empty();
		return;
	}
	
	TArray<FName> TargetChainNames;
	GetTargetChainNames(TargetChainNames);

	// remove all target chains that are no longer in the target IK rig asset
	TArray<FName> TargetChainsToRemove;
	for (FRetargetChainMap& ChainMap : Asset->ChainMapping)
	{
		if (!TargetChainNames.Contains(ChainMap.TargetChain))
		{
			TargetChainsToRemove.Add(ChainMap.TargetChain);
		}
	}
	for (FName TargetChainToRemove : TargetChainsToRemove)
	{
		Asset->ChainMapping.RemoveAll([&TargetChainToRemove](FRetargetChainMap& Element)
		{
			return Element.TargetChain == TargetChainToRemove;
		});
	}

	// add a mapping for each chain that is in the target IK rig (if it doesn't have one already)
	for (FName TargetChainName : TargetChainNames)
	{
		const bool HasChain = Asset->ChainMapping.ContainsByPredicate([&TargetChainName](FRetargetChainMap& Element)
		{
			return Element.TargetChain == TargetChainName;
		});
		
		if (!HasChain)
		{
			Asset->ChainMapping.Add(FRetargetChainMap(TargetChainName));
		}
	}

	TArray<FName> SourceChainNames;
	GetSourceChainNames(SourceChainNames);
	
	// reset any sources that are no longer present to "None"
	for (FRetargetChainMap& ChainMap : Asset->ChainMapping)
	{
		if (!SourceChainNames.Contains(ChainMap.SourceChain))
		{
			ChainMap.SourceChain = NAME_None;
		}
	}

	// enforce the chain order based on the StartBone index
	SortChainMapping();

	BroadcastNeedsReinitialized();
}

void UIKRetargeterController::CleanPoseList()
{
	// enforce the existence of a default pose
	const bool HasDefaultPose = Asset->RetargetPoses.Contains(Asset->DefaultPoseName);
	if (!HasDefaultPose)
	{
		Asset->RetargetPoses.Emplace(Asset->DefaultPoseName);
	}
	
	// use default pose unless set to something else
	if (Asset->CurrentRetargetPose == NAME_None)
	{
		Asset->CurrentRetargetPose = Asset->DefaultPoseName;
	}

	// remove all bone offsets that are no longer part of the target skeleton
	if (IsValid(Asset->TargetIKRigAsset))
	{
		const TArray<FName> AllowedBoneNames = Asset->TargetIKRigAsset->Skeleton.BoneNames;
		for (TTuple<FName, FIKRetargetPose>& Pose : Asset->RetargetPoses)
		{
			// find bone offsets no longer in target skeleton
			TArray<FName> BonesToRemove;
			for (TTuple<FName, FQuat>& BoneOffset : Pose.Value.BoneRotationOffsets)
			{
				if (!AllowedBoneNames.Contains(BoneOffset.Key))
				{
					BonesToRemove.Add(BoneOffset.Key);
				}
			}
			
			// remove bone offsets
			for (const FName& BoneToRemove : BonesToRemove)
			{
				Pose.Value.BoneRotationOffsets.Remove(BoneToRemove);
			}

			// sort the pose offset from leaf to root
			Pose.Value.SortHierarchically(Asset->TargetIKRigAsset->Skeleton);
		}
	}

	BroadcastNeedsReinitialized();
}

void UIKRetargeterController::AutoMapChains() const
{
	TArray<FName> SourceChainNames;
	GetSourceChainNames(SourceChainNames);
	
	// auto-map any chains that have no value using a fuzzy string search
	for (FRetargetChainMap& ChainMap : Asset->ChainMapping)
	{
		if (ChainMap.SourceChain != NAME_None)
		{
			continue; // already set by user
		}

		// find "best match" automatically as a convenience for the user
		FString TargetNameLowerCase = ChainMap.TargetChain.ToString().ToLower();
		float HighestScore = 0.2f;
		int32 HighestScoreIndex = -1;
		for (int32 ChainIndex=0; ChainIndex<SourceChainNames.Num(); ++ChainIndex)
		{
			FString SourceNameLowerCase = SourceChainNames[ChainIndex].ToString().ToLower();
			float WorstCase = TargetNameLowerCase.Len() + SourceNameLowerCase.Len();
			WorstCase = WorstCase < 1.0f ? 1.0f : WorstCase;
			const float Score = 1.0f - (Algo::LevenshteinDistance(TargetNameLowerCase, SourceNameLowerCase) / WorstCase);
			if (Score > HighestScore)
			{
				HighestScore = Score;
				HighestScoreIndex = ChainIndex;
			}
		}

		// apply source if any decent matches were found
		if (SourceChainNames.IsValidIndex(HighestScoreIndex))
		{
			ChainMap.SourceChain = SourceChainNames[HighestScoreIndex];
		}
	}

	// sort them
	SortChainMapping();

	// force update with latest mapping
	BroadcastNeedsReinitialized();
}

void UIKRetargeterController::OnRetargetChainRenamed(UIKRigDefinition* IKRig, FName OldChainName, FName NewChainName) const
{
	const bool bIsSourceRig = IKRig == Asset->SourceIKRigAsset;
	check(bIsSourceRig || IKRig == Asset->TargetIKRigAsset)
	for (FRetargetChainMap& ChainMap : Asset->ChainMapping)
	{
		FName& ChainNameToUpdate = bIsSourceRig ? ChainMap.SourceChain : ChainMap.TargetChain;
		if (ChainNameToUpdate == OldChainName)
		{
			ChainNameToUpdate = NewChainName;
			BroadcastNeedsReinitialized();
			return;
		}
	}
}

void UIKRetargeterController::OnRetargetChainRemoved(UIKRigDefinition* IKRig, const FName& InChainRemoved) const
{
	const bool bIsSourceRig = IKRig == Asset->SourceIKRigAsset;
	check(bIsSourceRig || IKRig == Asset->TargetIKRigAsset)

	// set source chain name to NONE if it has been deleted 
	if (bIsSourceRig)
	{
		for (FRetargetChainMap& ChainMap : Asset->ChainMapping)
		{
			FName& SourceChainToUpdate = ChainMap.SourceChain;
			if (SourceChainToUpdate == InChainRemoved)
			{
				SourceChainToUpdate = NAME_None;
				BroadcastNeedsReinitialized();
				return;
			}
		}
		return;
	}
	
	// remove target mapping if the target chain has been removed
	const int32 ChainIndex = Asset->ChainMapping.IndexOfByPredicate([&InChainRemoved](const FRetargetChainMap& ChainMap)
	{
		return ChainMap.TargetChain == InChainRemoved;
	});
	
	if (ChainIndex != INDEX_NONE)
	{
		Asset->ChainMapping.RemoveAt(ChainIndex);
		BroadcastNeedsReinitialized();
	}
}

void UIKRetargeterController::SetSourceChainForTargetChain(FName TargetChain, FName SourceChainToMapTo)
{
	FScopedTransaction Transaction(LOCTEXT("SetRetargetChainSource", "Set Retarget Chain Source"));
	Asset->Modify();
	
	FRetargetChainMap* ChainMap = GetChainMap(TargetChain);
	check(ChainMap)
	ChainMap->SourceChain = SourceChainToMapTo;
	BroadcastNeedsReinitialized();
}

FName UIKRetargeterController::GetSourceChainForTargetChain(FName TargetChain)
{
	FRetargetChainMap* ChainMap = GetChainMap(TargetChain);
	check(ChainMap)
	return ChainMap->SourceChain;
}

const TArray<FRetargetChainMap>& UIKRetargeterController::GetChainMappings()
{
	return Asset->ChainMapping;
}

USkeleton* UIKRetargeterController::GetSourceSkeletonAsset() const
{
	if (!IsValid(Asset->SourceIKRigAsset))
	{
		return nullptr;
	}

	if (!Asset->SourceIKRigAsset->PreviewSkeletalMesh)
	{
		return nullptr;
	}

	return Asset->SourceIKRigAsset->PreviewSkeletalMesh->GetSkeleton();
}

void UIKRetargeterController::AddRetargetPose(FName NewPoseName) const
{
	FScopedTransaction Transaction(LOCTEXT("AddRetargetPose", "Add Retarget Pose"));
	Asset->Modify();
	
	NewPoseName = MakePoseNameUnique(NewPoseName);
	Asset->RetargetPoses.Add(NewPoseName);
	Asset->CurrentRetargetPose = NewPoseName;

	BroadcastNeedsReinitialized();
}

void UIKRetargeterController::RemoveRetargetPose(FName PoseToRemove) const
{
	if (PoseToRemove == Asset->DefaultPoseName)
	{
		return; // cannot remove default pose
	}

	if (!Asset->RetargetPoses.Contains(PoseToRemove))
	{
		return; // cannot remove pose that doesn't exist
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveRetargetPose", "Remove Retarget Pose"));
	Asset->Modify();

	Asset->RetargetPoses.Remove(PoseToRemove);

	// did we remove the currently used pose?
	if (Asset->CurrentRetargetPose == PoseToRemove)
	{
		Asset->CurrentRetargetPose = UIKRetargeter::DefaultPoseName;
	}

	BroadcastNeedsReinitialized();
}

void UIKRetargeterController::ResetRetargetPose(FName PoseToReset) const
{
	if (!Asset->RetargetPoses.Contains(PoseToReset))
	{
		return; // cannot reset pose that doesn't exist
	}

	FScopedTransaction Transaction(LOCTEXT("ResetRetargetPose", "Reset Retarget Pose"));
	Asset->Modify();
	
	Asset->RetargetPoses[PoseToReset].BoneRotationOffsets.Reset();
	Asset->RetargetPoses[PoseToReset].RootTranslationOffset = FVector::ZeroVector;
	
	BroadcastNeedsReinitialized();
}

FName UIKRetargeterController::GetCurrentRetargetPoseName() const
{
	return GetAsset()->CurrentRetargetPose;
}

void UIKRetargeterController::SetCurrentRetargetPose(FName CurrentPose) const
{
	check(Asset->RetargetPoses.Contains(CurrentPose));

	FScopedTransaction Transaction(LOCTEXT("SetCurrentPose", "Set Current Pose"));
	Asset->Modify();
	Asset->CurrentRetargetPose = CurrentPose;
	
	BroadcastNeedsReinitialized();
}

const TMap<FName, FIKRetargetPose>& UIKRetargeterController::GetRetargetPoses()
{
	return GetAsset()->RetargetPoses;
}

void UIKRetargeterController::SetRotationOffsetForRetargetPoseBone(FName BoneName, FQuat RotationOffset) const
{
	const FIKRigSkeleton& Skeleton = Asset->GetTargetIKRig()->Skeleton;
	Asset->RetargetPoses[Asset->CurrentRetargetPose].SetBoneRotationOffset(BoneName, RotationOffset, Skeleton);
}

FQuat UIKRetargeterController::GetRotationOffsetForRetargetPoseBone(FName BoneName) const
{
	TMap<FName, FQuat>& BoneOffsets = Asset->RetargetPoses[Asset->CurrentRetargetPose].BoneRotationOffsets;
	if (!BoneOffsets.Contains(BoneName))
	{
		return FQuat::Identity;
	}
	
	return BoneOffsets[BoneName];
}

void UIKRetargeterController::AddTranslationOffsetToRetargetRootBone(FVector TranslationOffset) const
{
	Asset->RetargetPoses[Asset->CurrentRetargetPose].AddTranslationDeltaToRoot(TranslationOffset);
}

void UIKRetargeterController::SetEditRetargetPoseMode(bool bEditPoseMode, bool bReinitializeAfter) const
{
	GetAsset()->bEditRetargetPoseMode = bEditPoseMode;
	if (!bEditPoseMode && bReinitializeAfter)
	{
		// must reinitialize after editing the retarget pose
		BroadcastNeedsReinitialized();
	}
}

bool UIKRetargeterController::GetEditRetargetPoseMode() const
{
	return GetAsset()->bEditRetargetPoseMode;
}

FName UIKRetargeterController::MakePoseNameUnique(FName PoseName) const
{
	FName UniqueName = PoseName;
	int32 Suffix = 1;
	while (Asset->RetargetPoses.Contains(UniqueName))
	{
		UniqueName = FName(PoseName.ToString() + "_" + FString::FromInt(Suffix));
		++Suffix;
	}
	return UniqueName;
}

FRetargetChainMap* UIKRetargeterController::GetChainMap(const FName& TargetChainName) const
{
	for (FRetargetChainMap& ChainMap : Asset->ChainMapping)
	{
		if (ChainMap.TargetChain == TargetChainName)
		{
			return &ChainMap;
		}
	}

	return nullptr;
}

void UIKRetargeterController::SortChainMapping() const
{
	Asset->ChainMapping.Sort([this](const FRetargetChainMap& A, const FRetargetChainMap& B)
	{
		const TArray<FBoneChain>& BoneChains = Asset->TargetIKRigAsset->GetRetargetChains();
		const FIKRigSkeleton& TargetSkeleton = Asset->TargetIKRigAsset->Skeleton;

		// look for chains
		const int32 IndexA = BoneChains.IndexOfByPredicate([&A](const FBoneChain& Chain)
		{
			return A.TargetChain == Chain.ChainName;
		});

		const int32 IndexB = BoneChains.IndexOfByPredicate([&B](const FBoneChain& Chain)
		{
			return B.TargetChain == Chain.ChainName;
		});

		// compare their StartBone Index 
		if (IndexA > INDEX_NONE && IndexB > INDEX_NONE)
		{
			const int32 StartBoneIndexA = TargetSkeleton.GetBoneIndexFromName(BoneChains[IndexA].StartBone.BoneName);
			const int32 StartBoneIndexB = TargetSkeleton.GetBoneIndexFromName(BoneChains[IndexB].StartBone.BoneName);

			if (StartBoneIndexA == StartBoneIndexB)
			{
				// fallback to sorting alphabetically
				return BoneChains[IndexA].ChainName.LexicalLess(BoneChains[IndexB].ChainName);
			}
				
			return StartBoneIndexA < StartBoneIndexB;	
		}

		// sort them according to the target ik rig if previously failed 
		return IndexA < IndexB;
	});
}

#undef LOCTEXT_NAMESPACE
