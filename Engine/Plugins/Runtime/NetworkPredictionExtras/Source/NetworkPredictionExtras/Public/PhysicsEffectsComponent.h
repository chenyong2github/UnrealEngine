// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkPhysicsComponent.h"
#include "PhysicsEffect.h"
#include "Async/NetworkPredictionAsyncProxy.h"
#include "PhysicsEffectsComponent.generated.h"

// Networked Physics Component that can apply PhysicsEffects. This is intended to be a minimal implementation of the IPhysicsEffectsInterface.
// This common use case would be for objects that are not using PhysicsMovement but still need to apply PEs. Examples: grenades, rockets, etc.
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UPhysicsEffectsComponent : public UNetworkPhysicsComponent, public IPhysicsEffectsInterface
{
	GENERATED_BODY()

public:

	UPhysicsEffectsComponent();

	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker);

	UPROPERTY(Replicated)
	FNetworkPredictionAsyncProxy NetworkPredictionProxy;

	FNetworkPredictionAsyncProxy& GetNetworkPredictionAsyncProxy() override { return NetworkPredictionProxy; }
	FPhysicsEffectsExternalState& GetPhysicsEffectsExternalState() override { return PhysicsEffectsExternalState; }

	bool IsController() const override;

	// Forward input producing event to someone else (probably the owning actor)
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPhysicsEffect, int32, TypeID);
	
	// Delegate to inform the GT/bp that a PE has taken place and is now "seen" on the GT
	UPROPERTY(BlueprintAssignable, Category = "Physics Effect")
	FOnPhysicsEffect NotifyPhysicsEffectExecuted;
	void OnPhysicsEffectExecuted(uint8 TypeID) override { NotifyPhysicsEffectExecuted.Broadcast(TypeID); };

	// Delegate to inform the GT/bp  that a PE will take place soon but not hasn't happened yet
	UPROPERTY(BlueprintAssignable, Category = "Physics Effect")
	FOnPhysicsEffect NotifyPhysicsEffectWindUp;
	void OnPhysicsEffectWindUp(uint8 TypeID) override { NotifyPhysicsEffectWindUp.Broadcast(TypeID); };

private:

	FPhysicsEffectsExternalState PhysicsEffectsExternalState;
};