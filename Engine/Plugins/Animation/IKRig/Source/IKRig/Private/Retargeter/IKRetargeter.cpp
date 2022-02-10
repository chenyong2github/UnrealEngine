// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter/IKRetargeter.h"

const FName UIKRetargeter::DefaultPoseName = "Default Pose";
const FName UIKRetargeter::GetSourceIKRigPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, SourceIKRigAsset); };
const FName UIKRetargeter::GetTargetIKRigPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, TargetIKRigAsset); };
#if WITH_EDITOR
const FName UIKRetargeter::GetTargetPreviewMeshPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, TargetPreviewMesh); };
#endif
const FName UIKRetargeter::GetDefaultPoseName() { return DefaultPoseName; }

void UIKRetargeter::PostLoad()
{
	Super::PostLoad();

	// load deprecated chain mapping (pre UStruct to UObject refactor)
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!ChainMapping_DEPRECATED.IsEmpty())
	{
		for (const FRetargetChainMap& OldChainMap : ChainMapping_DEPRECATED)
		{
			if (OldChainMap.TargetChain == NAME_None)
			{
				continue;
			}
			
			TObjectPtr<URetargetChainSettings>* MatchingChain = ChainSettings.FindByPredicate([&](const URetargetChainSettings* Chain)
			{
				return Chain ? Chain->TargetChain == OldChainMap.TargetChain : false;
			});
			
			if (MatchingChain)
			{
				(*MatchingChain)->SourceChain = OldChainMap.SourceChain;
			}
			else
			{
				TObjectPtr<URetargetChainSettings> NewChainMap = NewObject<URetargetChainSettings>(this, URetargetChainSettings::StaticClass(), NAME_None, RF_Transactional);
				NewChainMap->TargetChain = OldChainMap.TargetChain;
				NewChainMap->SourceChain = OldChainMap.SourceChain;
				ChainSettings.Add(NewChainMap);
			}
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// remove null settings
	ChainSettings.Remove(nullptr);
}

void FIKRetargetPose::SetBoneRotationOffset(FName BoneName, FQuat RotationDelta, const FIKRigSkeleton& Skeleton)
{
	FQuat* RotOffset = BoneRotationOffsets.Find(BoneName);
	if (RotOffset == nullptr)
	{
		// first time this bone has been modified in this pose
		BoneRotationOffsets.Emplace(BoneName, RotationDelta);
		SortHierarchically(Skeleton);
		return;
	}

	*RotOffset = RotationDelta;
}

void FIKRetargetPose::AddTranslationDeltaToRoot(FVector TranslateDelta)
{
	RootTranslationOffset += TranslateDelta;
}

void FIKRetargetPose::SortHierarchically(const FIKRigSkeleton& Skeleton)
{
	// sort offsets hierarchically so that they are applied in leaf to root order
	// when generating the component space retarget pose in the processor
	BoneRotationOffsets.KeySort([Skeleton](FName A, FName B)
	{
		return Skeleton.GetBoneIndexFromName(A) > Skeleton.GetBoneIndexFromName(B);
	});
}
