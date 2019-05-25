// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_Layer.h"

FName FAnimNode_Layer::GetDynamicLinkFunctionName() const
{
	return Layer;
}

UAnimInstance* FAnimNode_Layer::GetDynamicLinkTarget(UAnimInstance* InOwningAnimInstance) const
{
	if(Interface.Get())
	{
		return GetTargetInstance<UAnimInstance>();
	}
	else
	{
		return InOwningAnimInstance;
	}
}

void FAnimNode_Layer::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	// We only initialize here if we are running a 'self' layer. Layers that use external instances need to be 
	// initialized by the owning anim instance as they may share sub-instances via grouping.
	if(Interface.Get() == nullptr || InstanceClass.Get() == nullptr)
	{
		UAnimInstance* CurrentTarget = GetTargetInstance<UAnimInstance>();

		USkeletalMeshComponent* MeshComp = InAnimInstance->GetSkelMeshComponent();
		check(MeshComp);

		if(LinkedRoot)
		{
			DynamicUnlink(const_cast<UAnimInstance*>(InAnimInstance));
		}

		// Switch from dynamic external to internal, kill old instance
		if(CurrentTarget && CurrentTarget != InAnimInstance)
		{
			MeshComp->SubInstances.Remove(CurrentTarget);
			CurrentTarget->MarkPendingKill();
			CurrentTarget = nullptr;
		}

		SetTargetInstance(const_cast<UAnimInstance*>(InAnimInstance));

		// Link before we call InitializeAnimation() so we propgate the call to sub-inputs
		DynamicLink(const_cast<UAnimInstance*>(InAnimInstance));

		InitializeProperties(InAnimInstance, InAnimInstance->GetClass());
	}
}

void FAnimNode_Layer::SetLayerOverlaySubInstance(const UAnimInstance* InOwningAnimInstance, UAnimInstance* InNewSubInstance)
{
	ReinitializeSubAnimInstance(InOwningAnimInstance, InNewSubInstance);
}
