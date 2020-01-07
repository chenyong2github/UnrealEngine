// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_LinkedAnimLayer.h"
#include "Animation/AnimClassInterface.h"

FName FAnimNode_LinkedAnimLayer::GetDynamicLinkFunctionName() const
{
	return Layer;
}

UAnimInstance* FAnimNode_LinkedAnimLayer::GetDynamicLinkTarget(UAnimInstance* InOwningAnimInstance) const
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

void FAnimNode_LinkedAnimLayer::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	// We only initialize here if we are running a 'self' layer. Layers that use external instances need to be 
	// initialized by the owning anim instance as they may share linked instances via grouping.
	if(Interface.Get() == nullptr || InstanceClass.Get() == nullptr)
	{
		InitializeSelfLayer(InAnimInstance);
	}
}

void FAnimNode_LinkedAnimLayer::InitializeSelfLayer(const UAnimInstance* SelfAnimInstance)
{
	UAnimInstance* CurrentTarget = GetTargetInstance<UAnimInstance>();

	IAnimClassInterface* PriorAnimBPClass = CurrentTarget ? IAnimClassInterface::GetFromClass(CurrentTarget->GetClass()) : nullptr;

	USkeletalMeshComponent* MeshComp = SelfAnimInstance->GetSkelMeshComponent();
	check(MeshComp);

	if (LinkedRoot)
	{
		DynamicUnlink(const_cast<UAnimInstance*>(SelfAnimInstance));
	}

	// Switch from dynamic external to internal, kill old instance
	if (CurrentTarget && CurrentTarget != SelfAnimInstance)
	{
		CurrentTarget->UninitializeAnimation();
		MeshComp->GetLinkedAnimInstances().Remove(CurrentTarget);
		CurrentTarget->MarkPendingKill();
		CurrentTarget = nullptr;
	}

	SetTargetInstance(const_cast<UAnimInstance*>(SelfAnimInstance));

	// Link before we call InitializeAnimation() so we propgate the call to linked input poses
	DynamicLink(const_cast<UAnimInstance*>(SelfAnimInstance));

	UClass* SelfClass = SelfAnimInstance->GetClass();
	InitializeProperties(SelfAnimInstance, SelfClass);

	IAnimClassInterface* NewAnimBPClass = IAnimClassInterface::GetFromClass(SelfClass);

	RequestBlend(PriorAnimBPClass, NewAnimBPClass);
}

void FAnimNode_LinkedAnimLayer::SetLinkedLayerInstance(const UAnimInstance* InOwningAnimInstance, UAnimInstance* InNewLinkedInstance)
{
	// Reseting to running as a self-layer, in case it is applicable
	if ((Interface.Get() == nullptr || InstanceClass.Get() == nullptr) && (InNewLinkedInstance == nullptr))
	{
		InitializeSelfLayer(InOwningAnimInstance);
	}
	else
	{
		ReinitializeLinkedAnimInstance(InOwningAnimInstance, InNewLinkedInstance);
	}

#if WITH_EDITOR
	OnInstanceChangedEvent.Broadcast();
#endif
}
