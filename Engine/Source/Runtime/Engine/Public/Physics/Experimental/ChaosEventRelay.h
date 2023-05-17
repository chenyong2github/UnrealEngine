// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Physics/Experimental/ChaosEventType.h"
#include "UObject/Object.h"

#include "ChaosEventRelay.generated.h"



DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBreakEventSignature, const TArray<FBreakChaosEvent>&, BreakEvents);


/**
* An object managing events
*/
UCLASS()
class ENGINE_API UChaosEventRelay : public UObject
{
	GENERATED_BODY()

public:

	UChaosEventRelay();

	void DispatchPhysicsBreakEvents(const TArray<FBreakChaosEvent>& BreakEvents);

	UPROPERTY(BlueprintAssignable, Category = "Physics Events")
	FBreakEventSignature OnBreakEvent;

};
