// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigAnimInstance.h"
#include "RigEditor/IKRigAnimInstanceProxy.h"

UIKRigAnimInstance::UIKRigAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseMultiThreadedAnimationUpdate = false;
}

void UIKRigAnimInstance::SetIKRigAsset(UIKRigDefinition* InIKRigAsset)
{
	FIKRigAnimInstanceProxy& Proxy = GetProxyOnGameThread<FIKRigAnimInstanceProxy>();
	Proxy.SetIKRigAsset(InIKRigAsset);
}

FAnimInstanceProxy* UIKRigAnimInstance::CreateAnimInstanceProxy()
{
	return new FIKRigAnimInstanceProxy(this);
}
