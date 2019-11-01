// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHeadTrackingNotificationsComponent.h"

UMagicLeapHeadTrackingNotificationsComponent::UMagicLeapHeadTrackingNotificationsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bAutoActivate = true;
}

void UMagicLeapHeadTrackingNotificationsComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TSet<EMagicLeapHeadTrackingMapEvent> CurrentMapEvents;
	if (UMagicLeapHMDFunctionLibrary::GetHeadTrackingMapEvents(CurrentMapEvents))
	{
		for (EMagicLeapHeadTrackingMapEvent MapEvent : CurrentMapEvents)
		{
			if (!PreviousMapEvents.Contains(MapEvent))
			{
				switch(MapEvent)
				{
					case EMagicLeapHeadTrackingMapEvent::Lost:
					{
						OnHeadTrackingLost.Broadcast();
						HMDLostDelegate.Broadcast();
						break;
					}
					case EMagicLeapHeadTrackingMapEvent::Recovered:
					{
						OnHeadTrackingRecovered.Broadcast();
						HMDReconnectedDelegate.Broadcast();
						break;
					}
					case EMagicLeapHeadTrackingMapEvent::RecoveryFailed:
					{
						OnHeadTrackingRecoveryFailed.Broadcast();
						break;
					}
					case EMagicLeapHeadTrackingMapEvent::NewSession:
					{
						OnHeadTrackingNewSessionStarted.Broadcast();
						HMDReconnectedDelegate.Broadcast();
						break;
					}
				}
			}
		}

		PreviousMapEvents = CurrentMapEvents;
	}
}

void UMagicLeapHeadTrackingNotificationsComponent::BindToOnHeadTrackingLostEvent(const FVRNotificationsDelegate& EventDelegate)
{
	OnHeadTrackingLost = EventDelegate;
}

void UMagicLeapHeadTrackingNotificationsComponent::BindToOnHeadTrackingRecoveredEvent(const FVRNotificationsDelegate& EventDelegate)
{
	OnHeadTrackingRecovered = EventDelegate;
}

void UMagicLeapHeadTrackingNotificationsComponent::BindToOnHeadTrackingRecoveryFailedEvent(const FVRNotificationsDelegate& EventDelegate)
{
	OnHeadTrackingRecoveryFailed = EventDelegate;
}

void UMagicLeapHeadTrackingNotificationsComponent::BindToOnHeadTrackingNewSessionStartedEvent(const FVRNotificationsDelegate& EventDelegate)
{
	OnHeadTrackingNewSessionStarted = EventDelegate;
}

void UMagicLeapHeadTrackingNotificationsComponent::UnbindOnHeadTrackingLostEvent()
{
	OnHeadTrackingLost.Clear();
}

void UMagicLeapHeadTrackingNotificationsComponent::UnbindOnHeadTrackingRecoveredEvent()
{
	OnHeadTrackingRecovered.Clear();
}

void UMagicLeapHeadTrackingNotificationsComponent::UnbindOnHeadTrackingRecoveryFailedEvent()
{
	OnHeadTrackingRecoveryFailed.Clear();
}

void UMagicLeapHeadTrackingNotificationsComponent::UnbindOnHeadTrackingNewSessionStartedEvent()
{
	OnHeadTrackingNewSessionStarted.Clear();
}
