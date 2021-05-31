// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDefinition.h"
#include "Engine/SkeletalMesh.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE	"IKRigDefinition"

FBoneChain* FRetargetDefinition::GetBoneChainByName(FName ChainName)
{
	for (FBoneChain& Chain : BoneChains)
	{
		if (Chain.ChainName == ChainName)
		{
			return &Chain;
		}
	}
	
	return nullptr;
}

void UIKRigDefinition::SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty)
{
	#if WITH_EDITOR
	if (bMarkAsDirty)
	{
		FScopedTransaction Transaction(LOCTEXT("SetPreviewMesh_Label", "Set Preview Mesh"));
		Modify();
		PreviewSkeletalMesh = PreviewMesh;
		return;
	}
	#endif
	
	PreviewSkeletalMesh = PreviewMesh;
}

USkeletalMesh* UIKRigDefinition::GetPreviewMesh() const
{
	return PreviewSkeletalMesh.Get();
}

#if WITH_EDITOR
bool UIKRigDefinition::Modify(bool bAlwaysMarkDirty /*=true*/)
{
	const bool bSavedToTransactionBuffer = Super::Modify(bAlwaysMarkDirty);
	if (bSavedToTransactionBuffer)
	{
		AssetVersion++; // inform any runtime/editor systems they should copy-up modifications	
	}
	return bSavedToTransactionBuffer;
}
#endif

void UIKRigDefinition::PostLoad()
{
	Super::PostLoad();
	ResetGoalTransforms();
}

void UIKRigDefinition::ResetGoalTransforms() const
{
	for (UIKRigEffectorGoal* Goal : Goals)
	{
		const FTransform InitialTransform =  GetGoalInitialTransform(Goal);
		Goal->InitialTransform = InitialTransform;
		Goal->CurrentTransform = InitialTransform;
	}	
}

FTransform UIKRigDefinition::GetGoalInitialTransform(UIKRigEffectorGoal* Goal) const
{
	if (!Goal)
	{
		return FTransform::Identity; // null
	}

	const int32 BoneIndex = Skeleton.GetBoneIndexFromName(Goal->BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return FTransform::Identity; // goal references unknown bone
	}

	return Skeleton.RefPoseGlobal[BoneIndex];
}

#undef LOCTEXT_NAMESPACE
