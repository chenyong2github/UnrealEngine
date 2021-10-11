// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CoreFwd.h"
#include "Async/NetworkPredictionAsyncModelDef.h"
#include "Async/NetworkPredictionAsyncID.h"
#include "PhysicsEffectTypes.h"
#include "PhysicsEffect.generated.h"


// Physics Effects is an NP system for doing generic physics effects like radial impulses.
// The goal is to provide a simple API for doing things on the PT without having to modify code.
// This is somewhat of a stop gap until a PT blueprint is available.
//
// The key here is that we are *computing* what forces/impulses/etc to apply on the PT.
// We already have a way to marshal absolute forces/impulses/etc from the GT but this is usually not what we want - we want to calculate these magnitudes based on the most accurate physics data/scene queries.
// Right now this is brutually rough and stubbed in. Will build up a more robust API in FPhysicsEffectDef.
//
// Note on the UE4 glue:
//	-Goal is to have the shared API on IPhysicsEffectsInterface.
//	-UPhysicsEffectComponent can be used to add this interface to any actor (e.g, a grenade)
//	-UPhysicsMovementComponent also implements IPhysicsEffectsInterface.
//	-We just don't want to put this on *every* physics object since their is additional overhead.


struct FNetworkPredictionAsyncProxy;

// State tracked on the GT for PEs
struct FPhysicsEffectsExternalState
{
	FPhysicsEffectCmd PendingEffectCmd;
	FPhysicsEffectNetState PhysicsEffectState;
	int32 CachedLastPlayedSimFrame = 0;
	int32 CachedWindUpSimFrame = 0;
};

USTRUCT(BlueprintType, meta=(ShowOnlyInnerProperties))
struct FPhysicsEffectParams
{
	GENERATED_BODY()

	// Length of time to apply this effect from when it starts. 0=single frame. Negative number for infinite duration. (Use ClearPhysicsEffect to stop it)
	UPROPERTY(BlueprintreadWrite, Category="Physics Effect")
	float DurationSeconds=0.0f;

	// Delay before starting the effect. Even a small amount of delay can help hide network latency to clients.
	UPROPERTY(BlueprintreadWrite, Category="Physics Effect")
	float DelaySeconds=0.f;

	UPROPERTY(BlueprintreadWrite, Category="Physics Effect")
	uint8 TypeID=0;
};

// Forward input producing event to someone else (probably the owning actor)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPhysicsEffects, int32, TypeID);

// UE Interface for Physics Effects
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UPhysicsEffectsInterface : public UInterface
{
	GENERATED_BODY()
};

class NETWORKPREDICTIONEXTRAS_API IPhysicsEffectsInterface
{
	GENERATED_BODY()

public:

	// Return the NP AsyncProxy to use for PhysicsEffects
	virtual FNetworkPredictionAsyncProxy& GetNetworkPredictionAsyncProxy() = 0;

	// Return the GT state for PhysicsEffects
	virtual FPhysicsEffectsExternalState& GetPhysicsEffectsExternalState() = 0;

	// Whether we are the controller of the PE system instance. Determines if we can write to InputCmds or do direct NetState mods.
	virtual bool IsController() const = 0;

	// Notify that a PE has executed (on the PT) and its output/side effects/(whatever it did) has been marshaled back (to the GT)
	virtual void OnPhysicsEffectExecuted(uint8 TypeID) { };

	// Notify a PE is pending execute but hasn't been marshaled back to the GT yet
	virtual void OnPhysicsEffectWindUp(uint8 TypeID) { };

	// Call in InitializeComponent() or anytime after the NP Async Proxy has been registered
	void InitializePhysicsEffects(FPhysicsEffectLocalState&& LocalState);

	// Call to sync with PT.
	void SyncPhysicsEffects();

	// ---------------------------------------------------------------------
	//	Temp/Debugging API
	// ---------------------------------------------------------------------

	// Returns latest copy of the active physics effect state. This is intended for debugging purposes. Function may be removed in the future.
	UFUNCTION(BlueprintCallable, Category="Physics Effect")
	virtual FPhysicsEffectNetState Debug_ReadActivePhysicsEffect() const;

	// Gets latest output sim frame. Debug only. Function may be removed in the future.
	UFUNCTION(BlueprintCallable, Category="Physics Effect")
	virtual int32 Debug_GetLatestSimFrame() const;

	// ---------------------------------------------------------------------
	//	Physics Effects API
	// ---------------------------------------------------------------------

	// Get Active Physics Effect TypeID. Can be used to determine "what is playing" (0 if nothing)
	UFUNCTION(BlueprintCallable, Category="Physics Effect")
	virtual uint8 GetActivePhysicsEffectTypeID() const;

	// Clears active PhysicsEffect
	UFUNCTION(BlueprintCallable, Category="Physics Effect")
	virtual void ClearPhysicsEffects();

	UFUNCTION(BlueprintCallable, Category="Physics Effect")
	virtual void ApplyImpulse(float Radius, float Magnitude, FTransform RelativeOffset, float FudgeVelocityZ, const FPhysicsEffectParams& Params);

private:

	void UpdatePendingWindUp(int32 Frame, uint8 TypeID);
	void ApplyPhysicsEffectDef(FPhysicsEffectDef&& Effect, const FPhysicsEffectParams& Params);
};


