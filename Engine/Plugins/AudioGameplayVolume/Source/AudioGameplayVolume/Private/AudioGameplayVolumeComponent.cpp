// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioGameplayVolumeComponent.h"
#include "AudioGameplayVolumeSubsystem.h"
#include "AudioGameplayVolumeProxy.h"
#include "AudioGameplayVolumeProxyMutator.h"
#include "Interfaces/IAudioGameplayVolumeInteraction.h"
#include "AudioDevice.h"

UAudioGameplayVolumeComponentBase::UAudioGameplayVolumeComponentBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAudioGameplayVolumeComponentBase::SetPriority(int32 InPriority)
{
	if (Priority != InPriority)
	{
		Priority = InPriority;
		NotifyDataChanged();
	}
}

TSharedPtr<FProxyVolumeMutator> UAudioGameplayVolumeComponentBase::CreateMutator() const
{
	TSharedPtr<FProxyVolumeMutator> ProxyMutator = FactoryMutator();
	if (ProxyMutator.IsValid())
	{
		FillMutator(ProxyMutator);
	}

	return ProxyMutator;
}

TSharedPtr<FProxyVolumeMutator> UAudioGameplayVolumeComponentBase::FactoryMutator() const
{
	return TSharedPtr<FProxyVolumeMutator>();
}

void UAudioGameplayVolumeComponentBase::FillMutator(TSharedPtr<FProxyVolumeMutator> Mutator) const
{
	check(Mutator.IsValid());
	Mutator->PayloadType = PayloadType;
	Mutator->Priority = Priority;
}

void UAudioGameplayVolumeComponentBase::NotifyDataChanged() const
{
	if (IsActive())
	{
		TInlineComponentArray<UAudioGameplayVolumeProxyComponent*> VolumeComponents(GetOwner());
		if (ensureMsgf(VolumeComponents.Num() == 1, TEXT("Expecting exactly one AudioGameplayVolumeProxyComponent on an actor!")))
		{
			if (VolumeComponents[0])
			{
				VolumeComponents[0]->OnComponentDataChanged();
			}
		}
	}
}

UAudioGameplayVolumeProxyComponent::UAudioGameplayVolumeProxyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = true;
}

void UAudioGameplayVolumeProxyComponent::SetProxy(UAudioGameplayVolumeProxy* NewProxy)
{
	RemoveProxy();
	Proxy = NewProxy;

	if (IsActive())
	{
		AddProxy();
	}
}

void UAudioGameplayVolumeProxyComponent::OnComponentDataChanged()
{
	if (IsActive())
	{
		UpdateProxy();
	}
}

void UAudioGameplayVolumeProxyComponent::EnterProxy() const
{
	TInlineComponentArray<UActorComponent*> ActorComponents(GetOwner());
	for (UActorComponent* ActorComponent : ActorComponents)
	{
		if (ActorComponent && ActorComponent->Implements<UAudioGameplayVolumeInteraction>())
		{
			IAudioGameplayVolumeInteraction::Execute_OnListenerEnter(ActorComponent);
		}
	}

	OnProxyEnter.Broadcast();
}

void UAudioGameplayVolumeProxyComponent::ExitProxy() const
{
	TInlineComponentArray<UActorComponent*> ActorComponents(GetOwner());
	for (UActorComponent* ActorComponent : ActorComponents)
	{
		if (ActorComponent && ActorComponent->Implements<UAudioGameplayVolumeInteraction>())
		{
			IAudioGameplayVolumeInteraction::Execute_OnListenerExit(ActorComponent);
		}
	}

	OnProxyExit.Broadcast();
}

#if WITH_EDITOR
void UAudioGameplayVolumeProxyComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAudioGameplayVolumeProxyComponent, Proxy))
	{
		RemoveProxy();

		if (IsActive())
		{
			AddProxy();
		}
	}
}
#endif // WITH_EDITOR

void UAudioGameplayVolumeProxyComponent::OnUnregister()
{
	Super::OnUnregister();
	RemoveProxy();
}

void UAudioGameplayVolumeProxyComponent::Enable()
{
	if (Proxy != nullptr)
	{
		Super::Enable();
		AddProxy();
	}
}

void UAudioGameplayVolumeProxyComponent::Disable()
{
	RemoveProxy();
	Super::Disable();
}

void UAudioGameplayVolumeProxyComponent::AddProxy() const
{
	if (UAudioGameplayVolumeSubsystem* VolumeSubsystem = GetSubsystem())
	{
		VolumeSubsystem->AddVolumeComponent(this);
	}
}

void UAudioGameplayVolumeProxyComponent::RemoveProxy() const
{
	if (UAudioGameplayVolumeSubsystem* VolumeSubsystem = GetSubsystem())
	{
		VolumeSubsystem->RemoveVolumeComponent(this);
	}
}

void UAudioGameplayVolumeProxyComponent::UpdateProxy() const
{
	if (UAudioGameplayVolumeSubsystem* VolumeSubsystem = GetSubsystem())
	{
		VolumeSubsystem->UpdateVolumeComponent(this);
	}
}

UAudioGameplayVolumeSubsystem* UAudioGameplayVolumeProxyComponent::GetSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		return FAudioDevice::GetSubsystem<UAudioGameplayVolumeSubsystem>(World->GetAudioDevice());
	}

	return nullptr;
}