// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargeterController.h"

#include "ScopedTransaction.h"
#include "Algo/LevenshteinDistance.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "RetargetEditor/IKRetargetEditorController.h"
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
	const bool bForceReinitialization = false;
	Controller->CleanChainMapping(bForceReinitialization);
	Controller->CleanPoseList(bForceReinitialization);
	
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

USkeletalMesh* UIKRetargeterController::GetSourcePreviewMesh() const
{
	// can't preview anything if target IK Rig is null
	if (!Asset->GetSourceIKRig())
	{
		return nullptr;
	}

	// optionally prefer override if one is provided
	if (!Asset->SourcePreviewMesh.IsNull())
	{
		return Asset->SourcePreviewMesh.LoadSynchronous();
	}

	// fallback to preview mesh from IK Rig asset
	return Asset->GetSourceIKRig()->GetPreviewMesh();
}

USkeletalMesh* UIKRetargeterController::GetTargetPreviewMesh() const
{
	// can't preview anything if target IK Rig is null
	if (!Asset->GetTargetIKRig())
	{
		return nullptr;
	}

	// optionally prefer override if one is provided
	if (!Asset->TargetPreviewMesh.IsNull())
	{
		return Asset->TargetPreviewMesh.LoadSynchronous();
	}

	// fallback to preview mesh from IK Rig asset
	return Asset->GetTargetIKRig()->GetPreviewMesh();
}

const UIKRigDefinition* UIKRetargeterController::GetSourceIKRig() const
{
	return Asset->GetSourceIKRig();
}

const UIKRigDefinition* UIKRetargeterController::GetTargetIKRig() const
{
	return Asset->GetTargetIKRig();
}

void UIKRetargeterController::OnTargetIKRigChanged() const
{
	if (const UIKRigDefinition* IKRig = GetTargetIKRig())
	{
		Asset->TargetPreviewMesh = IKRig->PreviewSkeletalMesh;
	}
}

void UIKRetargeterController::OnSourceIKRigChanged() const
{
	if (const UIKRigDefinition* IKRig = GetSourceIKRig())
	{
		Asset->SourcePreviewMesh = IKRig->PreviewSkeletalMesh;
	}
}

FName UIKRetargeterController::GetSourceRootBone() const
{
	const UIKRigDefinition* SourceIKRig = Asset->GetSourceIKRig();
	return SourceIKRig ? SourceIKRig->GetRetargetRoot() : FName("None");
}

FName UIKRetargeterController::GetTargetRootBone() const
{
	const UIKRigDefinition* TargetIKRig = Asset->GetTargetIKRig();
	return TargetIKRig ? TargetIKRig->GetRetargetRoot() : FName("None");
}

void UIKRetargeterController::GetTargetChainNames(TArray<FName>& OutNames) const
{
	if (const UIKRigDefinition* TargetIKRig = Asset->GetTargetIKRig())
	{
		const TArray<FBoneChain>& Chains = TargetIKRig->GetRetargetChains();
		for (const FBoneChain& Chain : Chains)
		{
			OutNames.Add(Chain.ChainName);
		}
	}
}

void UIKRetargeterController::GetSourceChainNames(TArray<FName>& OutNames) const
{
	if (const UIKRigDefinition* SourceIKRig = Asset->GetSourceIKRig())
	{
		const TArray<FBoneChain>& Chains = SourceIKRig->GetRetargetChains();
		for (const FBoneChain& Chain : Chains)
		{
			OutNames.Add(Chain.ChainName);
		}
	}
}

void UIKRetargeterController::CleanChainMapping(const bool bForceReinitialization) const
{
	if (IsValid(Asset->GetTargetIKRig()))
	{
		TArray<FName> TargetChainNames;
		GetTargetChainNames(TargetChainNames);

		// remove all target chains that are no longer in the target IK rig asset
		TArray<FName> TargetChainsToRemove;
		for (const URetargetChainSettings* ChainMap : Asset->ChainSettings)
		{
			if (!TargetChainNames.Contains(ChainMap->TargetChain))
			{
				TargetChainsToRemove.Add(ChainMap->TargetChain);
			}
		}
		for (FName TargetChainToRemove : TargetChainsToRemove)
		{
			Asset->ChainSettings.RemoveAll([&TargetChainToRemove](const URetargetChainSettings* Element)
			{
				return Element->TargetChain == TargetChainToRemove;
			});
		}

		// add a mapping for each chain that is in the target IK rig (if it doesn't have one already)
		for (FName TargetChainName : TargetChainNames)
		{
			const bool HasChain = Asset->ChainSettings.ContainsByPredicate([&TargetChainName](const URetargetChainSettings* Element)
			{
				return Element->TargetChain == TargetChainName;
			});
		
			if (!HasChain)
			{
				TObjectPtr<URetargetChainSettings> ChainMap = NewObject<URetargetChainSettings>(Asset, URetargetChainSettings::StaticClass(), NAME_None, RF_Transactional);
				ChainMap->TargetChain = TargetChainName;
				Asset->ChainSettings.Add(ChainMap);
			}
		}
	}

	if (IsValid(Asset->GetSourceIKRig()))
	{
		TArray<FName> SourceChainNames;
		GetSourceChainNames(SourceChainNames);
	
		// reset any sources that are no longer present to "None"
		for (URetargetChainSettings* ChainMap : Asset->ChainSettings)
		{
			if (!SourceChainNames.Contains(ChainMap->SourceChain))
			{
				ChainMap->SourceChain = NAME_None;
			}
		}
	}

	// enforce the chain order based on the StartBone index
	SortChainMapping();

	if (bForceReinitialization)
	{
		BroadcastNeedsReinitialized();
	}
}

void UIKRetargeterController::CleanPoseList(const bool bForceReinitialization)
{
	// enforce the existence of a default pose
	const bool HasDefaultPose = Asset->RetargetPoses.Contains(UIKRetargeter::GetDefaultPoseName());
	if (!HasDefaultPose)
	{
		Asset->RetargetPoses.Emplace(UIKRetargeter::GetDefaultPoseName());
	}
	
	// use default pose unless set to something else
	if (Asset->CurrentRetargetPose == NAME_None)
	{
		Asset->CurrentRetargetPose = UIKRetargeter::GetDefaultPoseName();
	}

	// remove all bone offsets that are no longer part of the target skeleton
	if (const UIKRigDefinition* TargetIKRig = Asset->GetTargetIKRig())
	{
		const TArray<FName> AllowedBoneNames = TargetIKRig->Skeleton.BoneNames;
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
			Pose.Value.SortHierarchically(TargetIKRig->Skeleton);
		}
	}

	if (bForceReinitialization)
	{
		BroadcastNeedsReinitialized();	
	}
}

void UIKRetargeterController::AutoMapChains() const
{
	TArray<FName> SourceChainNames;
	GetSourceChainNames(SourceChainNames);
	
	// auto-map any chains that have no value using a fuzzy string search
	for (URetargetChainSettings* ChainMap : Asset->ChainSettings)
	{
		if (ChainMap->SourceChain != NAME_None)
		{
			continue; // already set by user
		}

		// find "best match" automatically as a convenience for the user
		FString TargetNameLowerCase = ChainMap->TargetChain.ToString().ToLower();
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
			ChainMap->SourceChain = SourceChainNames[HighestScoreIndex];
		}
	}

	// sort them
	SortChainMapping();

	// force update with latest mapping
	BroadcastNeedsReinitialized();
}

void UIKRetargeterController::OnRetargetChainRenamed(UIKRigDefinition* IKRig, FName OldChainName, FName NewChainName) const
{
	const bool bIsSourceRig = IKRig == Asset->GetSourceIKRig();
	check(bIsSourceRig || IKRig == Asset->GetTargetIKRig())
	for (URetargetChainSettings* ChainMap : Asset->ChainSettings)
	{
		FName& ChainNameToUpdate = bIsSourceRig ? ChainMap->SourceChain : ChainMap->TargetChain;
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
	const bool bIsSourceRig = IKRig == Asset->GetSourceIKRig();
	check(bIsSourceRig || IKRig == Asset->GetTargetIKRig())

	// set source chain name to NONE if it has been deleted 
	if (bIsSourceRig)
	{
		for (URetargetChainSettings* ChainMap : Asset->ChainSettings)
		{
			if (ChainMap->SourceChain == InChainRemoved)
			{
				ChainMap->SourceChain = NAME_None;
				BroadcastNeedsReinitialized();
				return;
			}
		}
		return;
	}
	
	// remove target mapping if the target chain has been removed
	const int32 ChainIndex = Asset->ChainSettings.IndexOfByPredicate([&InChainRemoved](const URetargetChainSettings* ChainMap)
	{
		return ChainMap->TargetChain == InChainRemoved;
	});
	
	if (ChainIndex != INDEX_NONE)
	{
		Asset->ChainSettings.RemoveAt(ChainIndex);
		BroadcastNeedsReinitialized();
	}
}

void UIKRetargeterController::SetSourceChainForTargetChain(URetargetChainSettings* ChainMap, FName SourceChainToMapTo) const
{
	check(ChainMap)
	
	FScopedTransaction Transaction(LOCTEXT("SetRetargetChainSource", "Set Retarget Chain Source"));
	ChainMap->Modify();
	ChainMap->SourceChain = SourceChainToMapTo;
	
	BroadcastNeedsReinitialized();
}

const TArray<TObjectPtr<URetargetChainSettings>>& UIKRetargeterController::GetChainMappings() const
{
	return Asset->ChainSettings;
}

void UIKRetargeterController::AddRetargetPose(FName NewPoseName, const FIKRetargetPose* ToDuplicate) const
{
	FScopedTransaction Transaction(LOCTEXT("AddRetargetPose", "Add Retarget Pose"));
	Asset->Modify();
	
	NewPoseName = MakePoseNameUnique(NewPoseName.ToString());
	FIKRetargetPose& NewPose = Asset->RetargetPoses.Add(NewPoseName);
	if (ToDuplicate)
	{
		NewPose.RootTranslationOffset = ToDuplicate->RootTranslationOffset;
		NewPose.BoneRotationOffsets = ToDuplicate->BoneRotationOffsets;
	}
	Asset->CurrentRetargetPose = NewPoseName;

	BroadcastNeedsReinitialized();
}

void UIKRetargeterController::RenameCurrentRetargetPose(FName NewPoseName) const
{
	// do we already have a retarget pose with this name?
	if (Asset->RetargetPoses.Contains(NewPoseName))
	{
		return;
	}
	
	FScopedTransaction Transaction(LOCTEXT("RenameRetargetPose", "Rename Retarget Pose"));
	Asset->Modify();

	// replace key in the map
	TMap<FName, FIKRetargetPose>& Poses = Asset->RetargetPoses;
	const FIKRetargetPose CurrentPose = Poses[Asset->CurrentRetargetPose];
	Poses.Remove(Asset->CurrentRetargetPose);
	Poses.Shrink();
	Poses.Add(NewPoseName, CurrentPose);

	// update current pose name
	Asset->CurrentRetargetPose = NewPoseName;

	BroadcastNeedsReinitialized();
}

void UIKRetargeterController::RemoveRetargetPose(FName PoseToRemove) const
{
	if (PoseToRemove == Asset->GetDefaultPoseName())
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
		Asset->CurrentRetargetPose = UIKRetargeter::GetDefaultPoseName();
	}

	BroadcastNeedsReinitialized();
}

void UIKRetargeterController::ResetRetargetPose(FName PoseToReset, const TArray<FName>& BonesToReset) const
{
	if (!Asset->RetargetPoses.Contains(PoseToReset))
	{
		return; // cannot reset pose that doesn't exist
	}
	
	FIKRetargetPose& PoseToEdit = Asset->RetargetPoses[PoseToReset];
	
	if (BonesToReset.IsEmpty())
	{
		FScopedTransaction Transaction(LOCTEXT("ResetRetargetPose", "Reset Retarget Pose"));
		Asset->Modify();

		PoseToEdit.BoneRotationOffsets.Reset();
		PoseToEdit.RootTranslationOffset = FVector::ZeroVector;
	}
	else
	{
		FScopedTransaction Transaction(LOCTEXT("ResetRetargetBonePose", "Reset Bone Pose"));
		Asset->Modify();
		
		const FName RootBoneName = GetTargetRootBone();
		for (const FName& BoneToReset : BonesToReset)
		{
			if (PoseToEdit.BoneRotationOffsets.Contains(BoneToReset))
			{
				PoseToEdit.BoneRotationOffsets.Remove(BoneToReset);
			}

			if (BoneToReset == RootBoneName)
			{
				PoseToEdit.RootTranslationOffset = FVector::ZeroVector;	
			}
		}
	}

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

const FIKRetargetPose& UIKRetargeterController::GetCurrentRetargetPose() const
{
	return GetAsset()->RetargetPoses[GetCurrentRetargetPoseName()];
}

void UIKRetargeterController::SetRotationOffsetForRetargetPoseBone(FName BoneName, const FQuat& RotationOffset) const
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

void UIKRetargeterController::SetTranslationOffsetOnRetargetRootBone(FVector TranslationOffset) const
{
	Asset->RetargetPoses[Asset->CurrentRetargetPose].SetRootTranslationDelta(TranslationOffset);
}

const FVector& UIKRetargeterController::GetTranslationOffsetOnRetargetRootBone() const
{
	return Asset->RetargetPoses[Asset->CurrentRetargetPose].RootTranslationOffset;
}

void UIKRetargeterController::AddTranslationOffsetToRetargetRootBone(FVector TranslationOffset) const
{
	Asset->RetargetPoses[Asset->CurrentRetargetPose].AddToRootTranslationDelta(TranslationOffset);
}

FName UIKRetargeterController::MakePoseNameUnique(FString PoseName) const
{
	FString UniqueName = PoseName;
	int32 Suffix = 1;
	while (Asset->RetargetPoses.Contains(FName(UniqueName)))
	{
		UniqueName = PoseName + "_" + FString::FromInt(Suffix);
		++Suffix;
	}
	return FName(UniqueName);
}

URetargetChainSettings* UIKRetargeterController::GetChainMap(const FName& TargetChainName) const
{
	for (URetargetChainSettings* ChainMap : Asset->ChainSettings)
	{
		if (ChainMap->TargetChain == TargetChainName)
		{
			return ChainMap;
		}
	}

	return nullptr;
}

void UIKRetargeterController::SortChainMapping() const
{
	const UIKRigDefinition* TargetIKRig = Asset->GetTargetIKRig();
	if (!IsValid(TargetIKRig))
	{
		return;
	}
	
	Asset->ChainSettings.Sort([TargetIKRig](const URetargetChainSettings& A, const URetargetChainSettings& B)
	{
		const TArray<FBoneChain>& BoneChains = TargetIKRig->GetRetargetChains();
		const FIKRigSkeleton& TargetSkeleton = TargetIKRig->Skeleton;

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
