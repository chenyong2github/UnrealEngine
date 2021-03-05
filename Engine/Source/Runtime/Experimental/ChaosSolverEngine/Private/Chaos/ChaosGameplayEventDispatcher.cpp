// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosGameplayEventDispatcher.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "PhysicsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsCollisionHandler.h"
#include "ChaosStats.h"
#include "Chaos/ChaosNotifyHandlerInterface.h"
#include "EventsData.h"
#include "PhysicsEngine/BodySetup.h"
#include "EventManager.h"

FChaosBreakEvent::FChaosBreakEvent()
	: Component(nullptr)
	, Location(FVector::ZeroVector)
	, Velocity(FVector::ZeroVector)
	, AngularVelocity(FVector::ZeroVector)
	, Mass(0.0f)
{

}

void UChaosGameplayEventDispatcher::OnRegister()
{
	Super::OnRegister();
	RegisterChaosEvents();
}


void UChaosGameplayEventDispatcher::OnUnregister()
{
	UnregisterChaosEvents();
	Super::OnUnregister();
}

// internal
static void DispatchPendingBreakEvents(TArray<FChaosBreakEvent> const& Events, TMap<UPrimitiveComponent*, FBreakEventCallbackWrapper> const& Registrations)
{
	for (FChaosBreakEvent const& E : Events)
	{
		if (E.Component)
		{
			const FBreakEventCallbackWrapper* const Callback = Registrations.Find(E.Component);
			if (Callback)
			{
				Callback->BreakEventCallback(E);
			}
		}
	}
}

static void SetCollisionInfoFromComp(FRigidBodyCollisionInfo& Info, UPrimitiveComponent* Comp)
{
	if (Comp)
	{
		Info.Component = Comp;
		Info.Actor = Comp->GetOwner();
		
		const FBodyInstance* const BodyInst = Comp->GetBodyInstance();
		Info.BodyIndex = BodyInst ? BodyInst->InstanceBodyIndex : INDEX_NONE;
		Info.BoneName = BodyInst && BodyInst->BodySetup.IsValid() ? BodyInst->BodySetup->BoneName : NAME_None;
	}
	else
	{
		Info.Component = nullptr;
		Info.Actor = nullptr;
		Info.BodyIndex = INDEX_NONE;
		Info.BoneName = NAME_None;
	}
}


FCollisionNotifyInfo& UChaosGameplayEventDispatcher::GetPendingCollisionForContactPair(const void* P0, const void* P1, bool& bNewEntry)
{
	const FUniqueContactPairKey Key = { P0, P1 };
	const int32* PendingNotifyIdx = ContactPairToPendingNotifyMap.Find(Key);
	if (PendingNotifyIdx)
	{
		// we already have one for this pair
		bNewEntry = false;
		return PendingCollisionNotifies[*PendingNotifyIdx];
	}

	// make a new entry
	bNewEntry = true;
	int32 NewIdx = PendingCollisionNotifies.AddZeroed();
	return PendingCollisionNotifies[NewIdx];
}

FChaosPendingCollisionNotify& UChaosGameplayEventDispatcher::GetPendingChaosCollisionForContactPair(const void* P0, const void* P1, bool& bNewEntry)
{
	const FUniqueContactPairKey Key = { P0, P1 };
	const int32* PendingNotifyIdx = ContactPairToPendingChaosNotifyMap.Find(Key);
	if (PendingNotifyIdx)
	{
		// we already have one for this pair
		bNewEntry = false;
		return PendingChaosCollisionNotifies[*PendingNotifyIdx];
	}

	// make a new entry
	bNewEntry = true;
	int32 NewIdx = PendingChaosCollisionNotifies.AddZeroed();
	return PendingChaosCollisionNotifies[NewIdx];
}

void UChaosGameplayEventDispatcher::DispatchPendingCollisionNotifies()
{
	UWorld const* const OwningWorld = GetWorld();

	// Let the game-specific PhysicsCollisionHandler process any physics collisions that took place
	if (OwningWorld != nullptr && OwningWorld->PhysicsCollisionHandler != nullptr)
	{
		OwningWorld->PhysicsCollisionHandler->HandlePhysicsCollisions_AssumesLocked(PendingCollisionNotifies);
	}

	// Fire any collision notifies in the queue.
	for (FCollisionNotifyInfo& NotifyInfo : PendingCollisionNotifies)
	{
//		if (NotifyInfo.RigidCollisionData.ContactInfos.Num() > 0)
		{
			if (NotifyInfo.bCallEvent0 && /*NotifyInfo.IsValidForNotify() && */ NotifyInfo.Info0.Actor.IsValid())
			{
				NotifyInfo.Info0.Actor->DispatchPhysicsCollisionHit(NotifyInfo.Info0, NotifyInfo.Info1, NotifyInfo.RigidCollisionData);
			}

			// CHAOS: don't call event 1, because the code below will generate the reflexive hit data as separate entries
		}
	}
	for (FChaosPendingCollisionNotify& NotifyInfo : PendingChaosCollisionNotifies)
	{
		for (UObject* Obj : NotifyInfo.NotifyRecipients)
		{
			IChaosNotifyHandlerInterface* const Handler = Cast< IChaosNotifyHandlerInterface>(Obj);
			ensure(Handler);
			if (Handler)
			{
				Handler->HandlePhysicsCollision(NotifyInfo.CollisionInfo);
			}
		}
	}

	PendingCollisionNotifies.Reset();
	PendingChaosCollisionNotifies.Reset();
}

void UChaosGameplayEventDispatcher::RegisterForCollisionEvents(UPrimitiveComponent* ComponentToListenTo, UObject* ObjectToNotify)
{
	FChaosHandlerSet& HandlerSet = CollisionEventRegistrations.FindOrAdd(ComponentToListenTo);

	if (IChaosNotifyHandlerInterface* ChaosHandler = Cast<IChaosNotifyHandlerInterface>(ObjectToNotify))
	{
		HandlerSet.ChaosHandlers.Add(ObjectToNotify);
	}
	
	// a component can also implement the handler interface to get both types of events, so these aren't mutually exclusive
	if (ObjectToNotify == ComponentToListenTo)
	{
		HandlerSet.bLegacyComponentNotify = true;
	}

	// note: theoretically supportable to have external listeners to the legacy-style notifies, but will take more plumbing
}

void UChaosGameplayEventDispatcher::UnRegisterForCollisionEvents(UPrimitiveComponent* ComponentToListenTo, UObject* ObjectToNotify)
{
	FChaosHandlerSet* HandlerSet = CollisionEventRegistrations.Find(ComponentToListenTo);
	if (HandlerSet)
	{
		HandlerSet->ChaosHandlers.Remove(ObjectToNotify);

		if (ObjectToNotify == ComponentToListenTo)
		{
			HandlerSet->bLegacyComponentNotify = false;
		}

		if ((HandlerSet->ChaosHandlers.Num() == 0) && (HandlerSet->bLegacyComponentNotify == false))
		{
			// no one listening to this component any more, remove it entirely
			CollisionEventRegistrations.Remove(ComponentToListenTo);
		}
	}
}

void UChaosGameplayEventDispatcher::RegisterForBreakEvents(UPrimitiveComponent* Component, FOnBreakEventCallback InFunc)
{
	if (Component)
	{
		FBreakEventCallbackWrapper F = { InFunc };
		BreakEventRegistrations.Add(Component, F);
	}
}

void UChaosGameplayEventDispatcher::UnRegisterForBreakEvents(UPrimitiveComponent* Component)
{
	if (Component)
	{
		BreakEventRegistrations.Remove(Component);
	}
}

void UChaosGameplayEventDispatcher::DispatchPendingWakeNotifies()
{
	for (auto MapItr = PendingSleepNotifies.CreateIterator(); MapItr; ++MapItr)
	{
		FBodyInstance* BodyInstance = MapItr.Key();
		if (UPrimitiveComponent* PrimitiveComponent = BodyInstance->OwnerComponent.Get())
		{
			PrimitiveComponent->DispatchWakeEvents(MapItr.Value(), BodyInstance->BodySetup->BoneName);
		}
	}

	PendingSleepNotifies.Empty();
}

void UChaosGameplayEventDispatcher::RegisterChaosEvents()
{
#if WITH_CHAOS
	if (FPhysScene* Scene = GetWorld()->GetPhysicsScene())
	{
		if (Chaos::FPhysicsSolver* Solver = Scene->GetSolver())
		{
			Chaos::FEventManager* EventManager = Solver->GetEventManager();
			EventManager->RegisterHandler<Chaos::FCollisionEventData>(Chaos::EEventType::Collision, this, &UChaosGameplayEventDispatcher::HandleCollisionEvents);
			EventManager->RegisterHandler<Chaos::FBreakingEventData>(Chaos::EEventType::Breaking, this, &UChaosGameplayEventDispatcher::HandleBreakingEvents);
			EventManager->RegisterHandler<Chaos::FSleepingEventData>(Chaos::EEventType::Sleeping, this, &UChaosGameplayEventDispatcher::HandleSleepingEvents);
		}
	}

#endif
}

void UChaosGameplayEventDispatcher::UnregisterChaosEvents()
{
#if WITH_CHAOS
	if (GetWorld())
	{
		if (FPhysScene* Scene = GetWorld()->GetPhysicsScene())
		{
			if (Chaos::FPhysicsSolver* Solver = Scene->GetSolver())
			{
				Chaos::FEventManager* EventManager = Solver->GetEventManager();
				EventManager->UnregisterHandler(Chaos::EEventType::Collision, this);
				EventManager->UnregisterHandler(Chaos::EEventType::Breaking, this);
				EventManager->UnregisterHandler(Chaos::EEventType::Sleeping, this);
			}
		}
	}
#endif
}

void UChaosGameplayEventDispatcher::HandleCollisionEvents(const Chaos::FCollisionEventData& Event)
{
	SCOPE_CYCLE_COUNTER(STAT_DispatchCollisionEvents);

#if INCLUDE_CHAOS

	FPhysScene_Chaos& Scene = *(GetWorld()->GetPhysicsScene());

	PendingChaosCollisionNotifies.Reset();
	ContactPairToPendingNotifyMap.Reset();

	{
		TMap<IPhysicsProxyBase*, TArray<int32>> const& PhysicsProxyToCollisionIndicesMap = Event.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap;
		Chaos::FCollisionDataArray const& CollisionData = Event.CollisionData.AllCollisionsArray;

		int32 NumCollisions = CollisionData.Num();
		if (NumCollisions > 0)
		{
			// look through all the components that someone is interested in, and see if they had a collision
			// note that we only need to care about the interaction from the POV of the registered component,
			// since if anyone wants notifications for the other component it hit, it's also registered and we'll get to that elsewhere in the list
			for (TMap<UPrimitiveComponent*, FChaosHandlerSet>::TIterator It(CollisionEventRegistrations); It; ++It)
			{
				const FChaosHandlerSet& HandlerSet = It.Value();

				UPrimitiveComponent* const Comp0 = Cast<UPrimitiveComponent>(It.Key());
				const TArray<IPhysicsProxyBase*>* PhysicsProxyArray = Scene.GetOwnedPhysicsProxies(Comp0);

				if (PhysicsProxyArray)
				{
					for (IPhysicsProxyBase* PhysicsProxy0 : *PhysicsProxyArray)
					{
						TArray<int32> const* const CollisionIndices = PhysicsProxyToCollisionIndicesMap.Find(PhysicsProxy0);
						if (CollisionIndices)
						{
							for (int32 EncodedCollisionIdx : *CollisionIndices)
							{
								bool bSwapOrder;
								int32 CollisionIdx = Chaos::FEventManager::DecodeCollisionIndex(EncodedCollisionIdx, bSwapOrder);

								Chaos::FCollidingData const& CollisionDataItem = CollisionData[CollisionIdx];
								IPhysicsProxyBase* const PhysicsProxy1 = bSwapOrder ? CollisionDataItem.ParticleProxy : CollisionDataItem.LevelsetProxy;

								{
									bool bNewEntry = false;
									FCollisionNotifyInfo& NotifyInfo = GetPendingCollisionForContactPair(PhysicsProxy0, PhysicsProxy1, bNewEntry);

									// #note: we only notify on the first contact, though we will still accumulate the impulse data from subsequent contacts
									const FVector NormalImpulse = FVector::DotProduct(CollisionDataItem.AccumulatedImpulse, CollisionDataItem.Normal) * CollisionDataItem.Normal;	// project impulse along normal
									const FVector FrictionImpulse = FVector(CollisionDataItem.AccumulatedImpulse) - NormalImpulse; // friction is component not along contact normal
									NotifyInfo.RigidCollisionData.TotalNormalImpulse += NormalImpulse;
									NotifyInfo.RigidCollisionData.TotalFrictionImpulse += FrictionImpulse;

									if (bNewEntry)
									{
										UPrimitiveComponent* const Comp1 = Scene.GetOwningComponent<UPrimitiveComponent>(PhysicsProxy1);

										// fill in legacy contact data
										NotifyInfo.bCallEvent0 = true;
										// if Comp1 wants this event too, it will get its own pending collision entry, so we leave it false

										SetCollisionInfoFromComp(NotifyInfo.Info0, Comp0);
										SetCollisionInfoFromComp(NotifyInfo.Info1, Comp1);

										FRigidBodyContactInfo& NewContact = NotifyInfo.RigidCollisionData.ContactInfos.AddZeroed_GetRef();
										NewContact.ContactNormal = CollisionDataItem.Normal;
										NewContact.ContactPosition = CollisionDataItem.Location;
										NewContact.ContactPenetration = CollisionDataItem.PenetrationDepth;
										// NewContact.PhysMaterial[1] UPhysicalMaterial required here
									}

								}

								if (HandlerSet.ChaosHandlers.Num() > 0)
								{
									bool bNewEntry = false;
									FChaosPendingCollisionNotify& ChaosNotifyInfo = GetPendingChaosCollisionForContactPair(PhysicsProxy0, PhysicsProxy1, bNewEntry);

									// #note: we only notify on the first contact, though we will still accumulate the impulse data from subsequent contacts
									ChaosNotifyInfo.CollisionInfo.AccumulatedImpulse += CollisionDataItem.AccumulatedImpulse;

									if (bNewEntry)
									{
										UPrimitiveComponent* const Comp1 = Scene.GetOwningComponent<UPrimitiveComponent>(PhysicsProxy1);

										// fill in Chaos contact data
										ChaosNotifyInfo.CollisionInfo.Component = Comp0;
										ChaosNotifyInfo.CollisionInfo.OtherComponent = Comp1;
										ChaosNotifyInfo.CollisionInfo.Location = CollisionDataItem.Location;
										ChaosNotifyInfo.NotifyRecipients = HandlerSet.ChaosHandlers;

										if (bSwapOrder)
										{
											ChaosNotifyInfo.CollisionInfo.AccumulatedImpulse = -CollisionDataItem.AccumulatedImpulse;
											ChaosNotifyInfo.CollisionInfo.Normal = -CollisionDataItem.Normal;

											ChaosNotifyInfo.CollisionInfo.Velocity = CollisionDataItem.Velocity2;
											ChaosNotifyInfo.CollisionInfo.OtherVelocity = CollisionDataItem.Velocity1;
											ChaosNotifyInfo.CollisionInfo.AngularVelocity = CollisionDataItem.AngularVelocity2;
											ChaosNotifyInfo.CollisionInfo.OtherAngularVelocity = CollisionDataItem.AngularVelocity1;
											ChaosNotifyInfo.CollisionInfo.Mass = CollisionDataItem.Mass2;
											ChaosNotifyInfo.CollisionInfo.OtherMass = CollisionDataItem.Mass1;
										}
										else
										{
											ChaosNotifyInfo.CollisionInfo.AccumulatedImpulse = CollisionDataItem.AccumulatedImpulse;
											ChaosNotifyInfo.CollisionInfo.Normal = CollisionDataItem.Normal;

											ChaosNotifyInfo.CollisionInfo.Velocity = CollisionDataItem.Velocity1;
											ChaosNotifyInfo.CollisionInfo.OtherVelocity = CollisionDataItem.Velocity2;
											ChaosNotifyInfo.CollisionInfo.AngularVelocity = CollisionDataItem.AngularVelocity1;
											ChaosNotifyInfo.CollisionInfo.OtherAngularVelocity = CollisionDataItem.AngularVelocity2;
											ChaosNotifyInfo.CollisionInfo.Mass = CollisionDataItem.Mass1;
											ChaosNotifyInfo.CollisionInfo.OtherMass = CollisionDataItem.Mass2;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Tell the world and actors about the collisions
	DispatchPendingCollisionNotifies();

#endif
}

void UChaosGameplayEventDispatcher::HandleBreakingEvents(const Chaos::FBreakingEventData& Event)
{

	SCOPE_CYCLE_COUNTER(STAT_DispatchBreakEvents);

	// BREAK EVENTS

	TArray<FChaosBreakEvent> PendingBreakEvents;

	const float BreakingDataTimestamp = Event.BreakingData.TimeCreated;
	if (BreakingDataTimestamp > LastBreakingDataTime)
	{
		LastBreakingDataTime = BreakingDataTimestamp;

		Chaos::FBreakingDataArray const& BreakingData = Event.BreakingData.AllBreakingsArray;

		// let's assume breaks are very rare, so we will iterate breaks instead of registered components for now
		const int32 NumBreaks = BreakingData.Num();
		if (NumBreaks > 0)
		{
			for (Chaos::FBreakingData const& BreakingDataItem : BreakingData)
			{	
				if (BreakingDataItem.Particle && (BreakingDataItem.ParticleProxy))
				{
					UPrimitiveComponent* const PrimComp = Cast<UPrimitiveComponent>(BreakingDataItem.ParticleProxy->GetOwner());
					if (PrimComp && BreakEventRegistrations.Contains(PrimComp))
					{
						// queue them up so we can release the physics data before trigging BP events
						FChaosBreakEvent& BreakEvent = PendingBreakEvents.AddZeroed_GetRef();
							BreakEvent.Component = PrimComp;
							BreakEvent.Location = BreakingDataItem.Location;
							BreakEvent.Velocity = BreakingDataItem.Velocity;
							BreakEvent.AngularVelocity = BreakingDataItem.AngularVelocity;
							BreakEvent.Mass = BreakingDataItem.Mass;
					}
				}
			}

			DispatchPendingBreakEvents(PendingBreakEvents, BreakEventRegistrations);
		}
	}

}


void UChaosGameplayEventDispatcher::HandleSleepingEvents(const Chaos::FSleepingEventData& SleepingData)
{
	const Chaos::FSleepingDataArray& SleepingArray = SleepingData.SleepingData;

	for (const Chaos::FSleepingData& SleepData : SleepingArray)
	{
		if (SleepData.Particle->GetProxy()!= nullptr)
		{
			if (FBodyInstance* BodyInstance = FPhysicsUserData::Get<FBodyInstance>(SleepData.Particle->UserData()))
			{
				if (BodyInstance->bGenerateWakeEvents)
				{
					ESleepEvent WakeSleepEvent = SleepData.Sleeping ? ESleepEvent::SET_Sleep : ESleepEvent::SET_Wakeup;
					AddPendingSleepingNotify(BodyInstance, WakeSleepEvent);
				}
			}
		}
	}

	DispatchPendingWakeNotifies();
}


void UChaosGameplayEventDispatcher::AddPendingSleepingNotify(FBodyInstance* BodyInstance, ESleepEvent SleepEventType)
{
	PendingSleepNotifies.FindOrAdd(BodyInstance) = SleepEventType;
}
