// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassActorSubsystem.h"
#include "MassCommonTypes.h"
#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassSimulationSubsystem.h"
#include "VisualLogger/VisualLogger.h"

//----------------------------------------------------------------------//
//  FDataFragment_Actor 
//----------------------------------------------------------------------//

void FDataFragment_Actor::SetAndUpdateHandleMap(const FMassHandle MassAgent, AActor* InActor, const bool bInIsOwnedByMass)
{
	SetNoHandleMapUpdate(MassAgent, InActor, bInIsOwnedByMass);

	UWorld* World = InActor->GetWorld();
	check(World);
	if (UMassActorSubsystem* MassActorSubsystem = UWorld::GetSubsystem<UMassActorSubsystem>(World))
	{
		MassActorSubsystem->SetHandleForActor(InActor, MassAgent);
	}
}

void FDataFragment_Actor::ResetAndUpdateHandleMap()
{
	if (AActor* ActorPtr = Cast<AActor>(Actor.Get()))
	{
		UWorld* World = Actor->GetWorld();
		check(World);
		if (UMassActorSubsystem* MassActorSubsystem = UWorld::GetSubsystem<UMassActorSubsystem>(World))
		{
			MassActorSubsystem->RemoveHandleForActor(ActorPtr);
		}
	}

	ResetNoHandleMapUpdate();
}

void FDataFragment_Actor::SetNoHandleMapUpdate(const FMassHandle MassAgent, AActor* InActor, const bool bInIsOwnedByMass)
{
	check(InActor);
	check(!Actor.IsValid());
	check(MassAgent.IsValid());
	Actor = InActor;
	bIsOwnedByMass = bInIsOwnedByMass;
}

void FDataFragment_Actor::ResetNoHandleMapUpdate()
{
	Actor.Reset();
	bIsOwnedByMass = false;
}

//----------------------------------------------------------------------//
//  UMassActorSubsystem 
//----------------------------------------------------------------------//
void UMassActorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// making sure UMassSimulationSubsystem gets created before the MassActorManager
	Collection.InitializeDependency<UMassSimulationSubsystem>();
	
	EntitySystem = UWorld::GetSubsystem<UMassEntitySubsystem>(GetWorld());
}

FMassHandle UMassActorSubsystem::GetHandleFromActor(const TObjectKey<const AActor> Actor)
{
	UE_MT_SCOPED_READ_ACCESS(ActorHandleMapDetector);
	FLWEntity* LWEntity = ActorHandleMap.Find(Actor);
	if (!LWEntity)
	{
		return FMassHandle::InvalidHandle;
	}

	check(TObjectKey<const AActor>(GetActorFromHandle(FMassHandle(*LWEntity))) == Actor);
	return FMassHandle(*LWEntity);
}

AActor* UMassActorSubsystem::GetActorFromHandle(FMassHandle Handle) const
{
	check(EntitySystem);
	FDataFragment_Actor* Data = EntitySystem->GetComponentDataPtr<FDataFragment_Actor>(Handle.GetLWEntity());
	return Data != nullptr ? Data->GetMutable() : nullptr;
}

void UMassActorSubsystem::SetHandleForActor(const TObjectKey<const AActor> Actor, FMassHandle Handle)
{
	UE_MT_SCOPED_WRITE_ACCESS(ActorHandleMapDetector);
	ActorHandleMap.Add(Actor, Handle.GetLWEntity());
}

void UMassActorSubsystem::RemoveHandleForActor(const TObjectKey<const AActor> Actor)
{
	UE_MT_SCOPED_WRITE_ACCESS(ActorHandleMapDetector);
	ActorHandleMap.Remove(Actor);
}

void UMassActorSubsystem::DisconnectActor(const TObjectKey<const AActor> Actor, FMassHandle Handle)
{
	if (Handle.IsValid() == false)
	{
		return;
	}

	FLWEntity LWEntity;
	{
		UE_MT_SCOPED_WRITE_ACCESS(ActorHandleMapDetector);
		// We're assuming the Handle does match Actor, so we're RemoveAndCopyValue. If if doesn't we'll add it back.
		// The expectation is that this won't happen on a regular basis..
		if (ActorHandleMap.RemoveAndCopyValue(Actor, LWEntity) == false)
		{
			// the entity doesn't match the actor
			return;
		}
	}

	if (LWEntity == Handle.GetLWEntity())
	{
		check(EntitySystem);
		if (FDataFragment_Actor* Data = EntitySystem->GetComponentDataPtr<FDataFragment_Actor>(Handle.GetLWEntity()))
		{
			Data->ResetAndUpdateHandleMap();
		}
	}
	else
	{
		// unexpected mismatch. Add back and notify.
		UE_VLOG_UELOG(this, LogMass, Warning, TEXT("%s: Trying to disconnect actor %s while the Handle given doesn't match the system\'s records")
			, ANSI_TO_TCHAR(__FUNCTION__), *AActor::GetDebugName(Actor.ResolveObjectPtr()));
		SetHandleForActor(Actor, Handle);
	}
}