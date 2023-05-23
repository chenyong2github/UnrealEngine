// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/Experimental/ChaosEventRelay.h"

UChaosEventRelay::UChaosEventRelay()
{
}

void UChaosEventRelay::DispatchPhysicsCollisionEvents(const TArray<FCollisionChaosEvent>& CollisionEvents)
{
	
	if (OnCollisionEvent.IsBound())
	{
		OnCollisionEvent.Broadcast(CollisionEvents);
	}
}

void UChaosEventRelay::DispatchPhysicsBreakEvents(const TArray<FBreakChaosEvent>& BreakEvents)
{
	if (OnBreakEvent.IsBound())
	{
		OnBreakEvent.Broadcast(BreakEvents);
	}
}

void UChaosEventRelay::DispatchPhysicsRemovalEvents(const TArray<FRemovalChaosEvent>& RemovalEvents)
{
	if (OnRemovalEvent.IsBound())
	{
		OnRemovalEvent.Broadcast(RemovalEvents);
	}
}

void UChaosEventRelay::DispatchPhysicsCrumblingEvents(const TArray<FCrumblingChaosEvent>& CrumblingEvents)
{
	if (OnCrumblingEvent.IsBound())
	{
		OnCrumblingEvent.Broadcast(CrumblingEvents);
	}
}