// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkPhysics.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "NetworkPhysicsComponent.generated.h"

// Generic component that will register an actor's primitive component with the Network Physics system.
// This is nothing special about this component, it just provides the replicated NetworkPhysicsState and
// does the boiler plate registration with the Network Physics Manager. Native C++ classes or components
// may want to just do this themselves or inherit from the component. This is mainly a convenience for
// dropping in rollback physics support for blueprint actors.
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTION_API UNetworkPhysicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UNetworkPhysicsComponent();

	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;

	UFUNCTION(BlueprintPure, Category = "Network Physics")
	int32 GetNetworkPredictionLOD() const { return NetworkPhysicsStateArray.Num() > 0 ? NetworkPhysicsStateArray[0].LocalLOD : 0; }

	APlayerController* GetOwnerPC() const;

protected:
	
	FSingleParticlePhysicsProxy* GetManagedProxy() const { return NetworkPhysicsStateArray.Num() > 0 ? NetworkPhysicsStateArray[0].Proxy : nullptr; }
	
private:

	// Array of physics state being managed. Do not mess with this directly, it should only be touched in InitializeComponent
	UPROPERTY(Replicated, transient)
	TArray<FNetworkPhysicsState> NetworkPhysicsStateArray;
};