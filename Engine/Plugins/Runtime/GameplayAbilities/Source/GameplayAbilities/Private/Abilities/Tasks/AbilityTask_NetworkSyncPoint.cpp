// Copyright Epic Games, Inc. All Rights Reserved.

#include "Abilities/Tasks/AbilityTask_NetworkSyncPoint.h"
#include "AbilitySystemComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AbilityTask_NetworkSyncPoint)

UAbilityTask_NetworkSyncPoint::UAbilityTask_NetworkSyncPoint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ReplicatedEventToListenFor = EAbilityGenericReplicatedEvent::MAX;
}

void UAbilityTask_NetworkSyncPoint::OnSignalCallback()
{
	if (AbilitySystemComponent.IsValid())
	{
		AbilitySystemComponent->ConsumeGenericReplicatedEvent(ReplicatedEventToListenFor, GetAbilitySpecHandle(), GetActivationPredictionKey());
	}
	SyncFinished();
}

UAbilityTask_NetworkSyncPoint* UAbilityTask_NetworkSyncPoint::WaitNetSync(class UGameplayAbility* OwningAbility, EAbilityTaskNetSyncType InSyncType)
{
	UAbilityTask_NetworkSyncPoint* MyObj = NewAbilityTask<UAbilityTask_NetworkSyncPoint>(OwningAbility);
	MyObj->SyncType = InSyncType;
	return MyObj;
}

void UAbilityTask_NetworkSyncPoint::Activate()
{
	if (AbilitySystemComponent.IsValid())
	{
		if (IsPredictingClient())
		{
			if (SyncType != EAbilityTaskNetSyncType::OnlyServerWait)
			{
				// As long as we are waiting (!= OnlyServerWait), listen for the GenericSignalFromServer event
				ReplicatedEventToListenFor = EAbilityGenericReplicatedEvent::GenericSignalFromServer;
			}
			if (SyncType != EAbilityTaskNetSyncType::OnlyClientWait)
			{
				// @note: When on a predicting client we need to make sure we send off the scoped prediction key to the server so things like gameplay
				// cue's that replicate down will continue to not execute on a client that is predicting abilities.
				FScopedPredictionWindow ScopedPrediction(AbilitySystemComponent.Get(), IsPredictingClient());

				// As long as the server is waiting (!= OnlyClientWait), send the Server and RPC for this signal
				AbilitySystemComponent->ServerSetReplicatedEvent(EAbilityGenericReplicatedEvent::GenericSignalFromClient, GetAbilitySpecHandle(), GetActivationPredictionKey(), AbilitySystemComponent->ScopedPredictionKey);
			}

		}
		else if (IsForRemoteClient())
		{
			if (SyncType != EAbilityTaskNetSyncType::OnlyClientWait)
			{
				// As long as we are waiting (!= OnlyClientWait), listen for the GenericSignalFromClient event
				ReplicatedEventToListenFor = EAbilityGenericReplicatedEvent::GenericSignalFromClient;
			}
			if (SyncType != EAbilityTaskNetSyncType::OnlyServerWait)
			{
				// As long as the client is waiting (!= OnlyServerWait), send the Server and RPC for this signal
				AbilitySystemComponent->ClientSetReplicatedEvent(EAbilityGenericReplicatedEvent::GenericSignalFromServer, GetAbilitySpecHandle(), GetActivationPredictionKey());
			}
		}

		if (ReplicatedEventToListenFor != EAbilityGenericReplicatedEvent::MAX)
		{
			CallOrAddReplicatedDelegate(ReplicatedEventToListenFor, FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UAbilityTask_NetworkSyncPoint::OnSignalCallback));
		}
		else
		{
			// We aren't waiting for a replicated event, so the sync is complete.
			SyncFinished();
		}
	}
}

void UAbilityTask_NetworkSyncPoint::SyncFinished()
{
	if (IsValid(this))
	{
		// @note: When we finish the task to let the ability resume execution we need to ensure predicting clients have a valid
		// scoped prediction key so they can continue to execute things like gameplay cues. This is because the server will pass
		// along a client prediction key through its replication chain ensuring predicted abilities don't execute predicted
		// features (i.e. gameplay cues)
		FScopedPredictionWindow ScopedPrediction(AbilitySystemComponent.Get(), IsPredictingClient());

		if (ShouldBroadcastAbilityTaskDelegates())
		{
			OnSync.Broadcast();
		}
		EndTask();
	}
}

