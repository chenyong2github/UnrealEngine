// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditorController.h"

#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/SIKRetargetChainMapList.h"
#include "Retargeter/IKRetargeter.h"
#include "RigEditor/IKRigController.h"

#define LOCTEXT_NAMESPACE "IKRetargetEditorController"

void FIKRetargetEditorController::Initialize(TSharedPtr<FIKRetargetEditor> InEditor, UIKRetargeter* InAsset)
{
	Editor = InEditor;
	AssetController = UIKRetargeterController::GetController(InAsset);

	// bind callbacks when SOURCE or TARGET IK Rigs are modified
	BindToIKRigAsset(AssetController->GetAsset()->SourceIKRigAsset);
	BindToIKRigAsset(AssetController->GetAsset()->TargetIKRigAsset);

	// bind callback when retargeter needs reinitialized
	AssetController->OnRetargeterNeedsInitialized().AddSP(this, &FIKRetargetEditorController::OnRetargeterNeedsInitialized);
}

void FIKRetargetEditorController::BindToIKRigAsset(UIKRigDefinition* InIKRig)
{
	if (!InIKRig)
	{
		return;
	}

	UIKRigController* Controller = UIKRigController::GetIKRigController(InIKRig);
	if (!Controller->OnIKRigNeedsInitialized().IsBoundToObject(this))
	{
		Controller->OnIKRigNeedsInitialized().AddSP(this, &FIKRetargetEditorController::OnIKRigNeedsInitialized);
		Controller->OnRetargetChainRenamed().AddSP(this, &FIKRetargetEditorController::OnRetargetChainRenamed);
	}
}

void FIKRetargetEditorController::OnIKRigNeedsInitialized(UIKRigDefinition* ModifiedIKRig)
{
	const UIKRetargeter* Retargeter = AssetController->GetAsset();
	
	check(ModifiedIKRig && Retargeter)
	 
	const bool bIsSource = ModifiedIKRig == Retargeter->SourceIKRigAsset;
	const bool bIsTarget = ModifiedIKRig == Retargeter->TargetIKRigAsset;
	if (!(bIsSource || bIsTarget))
	{
		return;
	}

	// the target anim instance has the IK RetargetPoseFromMesh node which needs reinitialized with new asset version
	TargetAnimInstance->SetProcessorNeedsInitialized();
	RefreshAllViews();
}

void FIKRetargetEditorController::OnRetargetChainRenamed(UIKRigDefinition* ModifiedIKRig, FName OldName, FName NewName)
{
	check(ModifiedIKRig)
	
	AssetController->OnRetargetChainRenamed(ModifiedIKRig, OldName, NewName);
}

void FIKRetargetEditorController::OnRetargeterNeedsInitialized(const UIKRetargeter* Retargeter)
{
	TargetAnimInstance->SetProcessorNeedsInitialized();
	RefreshAllViews();
}

USkeletalMesh* FIKRetargetEditorController::GetSourceSkeletalMesh() const
{
	if (!(AssetController && AssetController->GetAsset()->SourceIKRigAsset))
	{
		return nullptr;
	}

	return AssetController->GetAsset()->SourceIKRigAsset->PreviewSkeletalMesh;
}

USkeletalMesh* FIKRetargetEditorController::GetTargetSkeletalMesh() const
{
	if (!(AssetController && AssetController->GetAsset()->TargetIKRigAsset))
	{
		return nullptr;
	}

	return AssetController->GetAsset()->TargetIKRigAsset->PreviewSkeletalMesh;
}

FTransform FIKRetargetEditorController::GetTargetBoneTransform(const int32& TargetBoneIndex) const
{
	UIKRetargetAnimInstance* AnimInstance = TargetAnimInstance.Get();
	check(AnimInstance);

	const UIKRetargetProcessor* RetargetProcessor = AnimInstance->GetRetargetProcessor();
	check(RetargetProcessor)

	// get transform of root of chain
	FTransform BoneTransform = RetargetProcessor->GetTargetBoneRetargetPoseGlobalTransform(TargetBoneIndex);

	// scale and offset
	BoneTransform.ScaleTranslation(AssetController->GetAsset()->TargetActorScale);
	BoneTransform.AddToTranslation(FVector(AssetController->GetAsset()->TargetActorOffset, 0.f, 0.f));

	return BoneTransform;
}

bool FIKRetargetEditorController::GetTargetBoneLineSegments(
	const int32& TargetBoneIndex,
	FVector& OutStart,
	TArray<FVector>& OutChildren) const
{
	// get the runtime processor
	const UIKRetargetProcessor* Processor = GetRetargetProcessor();
	check(Processor && Processor->IsInitialized())
	
	// get the target skeleton we want to draw
	const FTargetSkeleton& TargetSkeleton = Processor->GetTargetSkeleton();
	check(TargetSkeleton.BoneNames.IsValidIndex(TargetBoneIndex))

	// get the origin of the bone chain
	OutStart = TargetSkeleton.RetargetGlobalPose[TargetBoneIndex].GetTranslation();

	// get children
	TArray<int32> ChildIndices;
	TargetSkeleton.GetChildrenIndices(TargetBoneIndex, ChildIndices);
	for (const int32& ChildIndex : ChildIndices)
	{
		OutChildren.Emplace(TargetSkeleton.RetargetGlobalPose[ChildIndex].GetTranslation());
	}

	// add the target translation offset and scale
	const FVector TargetOffset(AssetController->GetAsset()->TargetActorOffset, 0.f, 0.f);
	OutStart *= AssetController->GetAsset()->TargetActorScale;
	OutStart += TargetOffset;
	for (FVector& ChildPoint : OutChildren)
	{
		ChildPoint *= AssetController->GetAsset()->TargetActorScale;
		ChildPoint += TargetOffset;
	}
	
	return true;
}

bool FIKRetargetEditorController::IsTargetBoneRetargeted(const int32& TargetBoneIndex)
{
	// get the runtime processor
	const UIKRetargetProcessor* Processor = GetRetargetProcessor();
	check(Processor && Processor->IsInitialized())
	
	// get the target skeleton
	const FTargetSkeleton& TargetSkeleton = Processor->GetTargetSkeleton();
	check(TargetSkeleton.BoneNames.IsValidIndex(TargetBoneIndex))

	return TargetSkeleton.IsBoneRetargeted[TargetBoneIndex];
}

const UIKRetargetProcessor* FIKRetargetEditorController::GetRetargetProcessor() const
{	
	if(UIKRetargetAnimInstance* AnimInstance = TargetAnimInstance.Get())
	{
		return AnimInstance->GetRetargetProcessor();
	}

	return nullptr;	
}

void FIKRetargetEditorController::RefreshAllViews()
{
	ChainsView.Get()->RefreshView();
}

void FIKRetargetEditorController::PlayAnimationAsset(UAnimationAsset* AssetToPlay)
{
	if (AssetToPlay && SourceAnimInstance)
	{
		SourceAnimInstance->SetAnimationAsset(AssetToPlay);
		PreviousAsset = AssetToPlay;
	}
}

void FIKRetargetEditorController::PlayPreviousAnimationAsset()
{
	if (PreviousAsset)
	{
		SourceAnimInstance->SetAnimationAsset(PreviousAsset);
	}
}

#undef LOCTEXT_NAMESPACE
