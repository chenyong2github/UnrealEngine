// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetAnimInstanceProxy.h"

UIKRetargetAnimInstance::UIKRetargetAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseMultiThreadedAnimationUpdate = false;
}

void UIKRetargetAnimInstance::SetRetargetAssetAndSourceComponent(
	UIKRetargeter* InAsset,
	TWeakObjectPtr<USkeletalMeshComponent> InSourceMeshComponent)
{
	FIKRetargetAnimInstanceProxy& Proxy = GetProxyOnGameThread<FIKRetargetAnimInstanceProxy>();
	Proxy.SetRetargetAssetAndSourceComponent(InAsset, InSourceMeshComponent);
}

UIKRetargeter* UIKRetargetAnimInstance::GetCurrentlyUsedRetargeter() const
{
	return IKRetargeterNode.GetCurrentlyUsedRetargeter();
}

FAnimInstanceProxy* UIKRetargetAnimInstance::CreateAnimInstanceProxy()
{
	return new FIKRetargetAnimInstanceProxy(this, &IKRetargeterNode);
}
