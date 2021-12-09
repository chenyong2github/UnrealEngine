// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetAnimInstanceProxy.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"


FIKRetargetAnimInstanceProxy::FIKRetargetAnimInstanceProxy(UAnimInstance* InAnimInstance, FAnimNode_RetargetPoseFromMesh* InNode)
	: FAnimPreviewInstanceProxy(InAnimInstance),
	IKRetargetNode(InNode)
{
}

void FIKRetargetAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimPreviewInstanceProxy::Initialize(InAnimInstance);
}

bool FIKRetargetAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	IKRetargetNode->Evaluate_AnyThread(Output);
	return true;
}

FAnimNode_Base* FIKRetargetAnimInstanceProxy::GetCustomRootNode()
{
	return IKRetargetNode;
}

void FIKRetargetAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(IKRetargetNode);
}

void FIKRetargetAnimInstanceProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	if (CurrentAsset != nullptr)
	{
		FAnimPreviewInstanceProxy::UpdateAnimationNode(InContext);
	}
	else
	{
		IKRetargetNode->Update_AnyThread(InContext);
	}
}

void FIKRetargetAnimInstanceProxy::SetRetargetAssetAndSourceComponent(
	UIKRetargeter* InIKRetargetAsset,
	TWeakObjectPtr<USkeletalMeshComponent> InSourceMeshComponent) const
{
	IKRetargetNode->IKRetargeterAsset = InIKRetargetAsset;
	IKRetargetNode->bUseAttachedParent = false;
	IKRetargetNode->SourceMeshComponent = InSourceMeshComponent;
	IKRetargetNode->bDriveTargetIKRigWithAsset = true;
	IKRetargetNode->SetProcessorNeedsInitialized();
}

