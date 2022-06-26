// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioGameplayVolumeComponent.h"
#include "AudioGameplayVolumeSubsystem.h"
#include "AudioGameplayVolumeProxy.h"
#include "Interfaces/IAudioGameplayVolumeInteraction.h"
#include "AudioDevice.h"

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