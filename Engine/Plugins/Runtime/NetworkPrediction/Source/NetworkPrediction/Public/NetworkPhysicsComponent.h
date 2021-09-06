// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkPhysics.h"
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

	UPROPERTY(Replicated, transient)
	FNetworkPhysicsState NetworkPhysicsState;

	UFUNCTION(BlueprintPure, Category = "Network Physics")
	int32 GetNetworkPredictionLOD() const { return NetworkPhysicsState.LocalLOD; }

	APlayerController* GetOwnerPC() const;

protected:

	FSingleParticlePhysicsProxy* GetManagedProxy() const { return NetworkPhysicsState.Proxy; }

	// Which component's physics body to manage if there are multiple PrimitiveComponent and not the root component.
	UPROPERTY(EditDefaultsOnly, Category = "Network Physics")
	FName ManagedComponentTag=NAME_None;
};