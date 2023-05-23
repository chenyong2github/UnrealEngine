// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Physics/Experimental/ChaosEventType.h"
#include "PhysicsPublic.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"

#include "ChaosEventRelay.generated.h"



DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCollisionEventSignature, const TArray<FCollisionChaosEvent>&, CollisionEvents);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBreakEventSignature, const TArray<FBreakChaosEvent>&, BreakEvents);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRemovalEventSignature, const TArray<FRemovalChaosEvent>&, RemovalEvents);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrumblingEventSignature, const TArray<FCrumblingChaosEvent>&, CrumblingEvents);


/**
* An object managing events
*/
UCLASS()
class ENGINE_API UChaosEventRelay : public UObject
{
	GENERATED_BODY()

public:

	UChaosEventRelay();

	void DispatchPhysicsCollisionEvents(const TArray<FCollisionChaosEvent>& CollisionEvents);
	void DispatchPhysicsBreakEvents(const TArray<FBreakChaosEvent>& BreakEvents);
	void DispatchPhysicsRemovalEvents(const TArray<FRemovalChaosEvent>& RemovalEvents);
	void DispatchPhysicsCrumblingEvents(const TArray<FCrumblingChaosEvent>& CrumblingEvents);

	
	UPROPERTY(BlueprintAssignable, Category = "Physics Events")
	FCollisionEventSignature OnCollisionEvent;

	UPROPERTY(BlueprintAssignable, Category = "Physics Events")
	FBreakEventSignature OnBreakEvent;

	UPROPERTY(BlueprintAssignable, Category = "Physics Events")
	FRemovalEventSignature OnRemovalEvent;

	UPROPERTY(BlueprintAssignable, Category = "Physics Events")
	FCrumblingEventSignature OnCrumblingEvent;

};
