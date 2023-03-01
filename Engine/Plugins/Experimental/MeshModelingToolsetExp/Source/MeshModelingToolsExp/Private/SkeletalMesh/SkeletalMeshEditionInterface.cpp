// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMesh/SkeletalMeshEditionInterface.h"

IMPLEMENT_HIT_PROXY(HBoneHitProxy, HHitProxy)

ISkeletalMeshNotifier& ISkeletalMeshEditionInterface::GetNotifier()
{
	if (!Notifier)
	{
		Notifier.Reset(new FSkeletalMeshToolNotifier(TWeakInterfacePtr<ISkeletalMeshEditionInterface>(this)));
	}
	
	return *Notifier;
}

bool ISkeletalMeshEditionInterface::NeedsNotification() const
{
	return Notifier && Notifier->Delegate().IsBound(); 
}

void ISkeletalMeshEditionInterface::BindTo(TSharedPtr<ISkeletalMeshEditorBinding> InBinding)
{
	Binding = InBinding;
}

void ISkeletalMeshEditionInterface::Unbind()
{
	Binding.Reset();
}

TOptional<FName> ISkeletalMeshEditionInterface::GetBoneName(HHitProxy* InHitProxy) const
{
	if (const HBoneHitProxy* BoneProxy = HitProxyCast<HBoneHitProxy>(InHitProxy))
	{
		return BoneProxy->BoneName;
	}

	return Binding.IsValid() && Binding.Pin()->GetNameFunction() ? Binding.Pin()->GetNameFunction()(InHitProxy) : TOptional<FName>();
}

FSkeletalMeshToolNotifier::FSkeletalMeshToolNotifier(TWeakInterfacePtr<ISkeletalMeshEditionInterface> InInterface)
	: ISkeletalMeshNotifier()
	, Interface(InInterface)
{}

void FSkeletalMeshToolNotifier::HandleNotification(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
{
	if (Interface.IsValid())
	{
		Interface->HandleSkeletalMeshModified(BoneNames, InNotifyType);
	}
}
