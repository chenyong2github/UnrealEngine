// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionPhysics.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Chaos/ParticleHandle.h"
#include "Components/PrimitiveComponent.h"

#if WITH_CHAOS

// -------------------------------------------------------------------------------------------------------------------------
//	Interpolation related functions. These require calls to the UPrimitiveComponent and cannot be implemented via FBodyInstance
//
//	If you are landing here with a nullptr Driver, see notes in FNetworkPredictionDriverBase::SafeCastDriverToPrimitiveComponent
// -------------------------------------------------------------------------------------------------------------------------

void FNetworkPredictionPhysicsState::BeginInterpolation(UPrimitiveComponent* Driver)
{
	npCheckSlow(Driver);
	Driver->SetSimulatePhysics(false);
}

void FNetworkPredictionPhysicsState::EndInterpolation(UPrimitiveComponent* Driver)
{
	npCheckSlow(Driver);
	Driver->SetSimulatePhysics(true);
}

void FNetworkPredictionPhysicsState::FinalizeInterpolatedPhysics(UPrimitiveComponent* Driver, FNetworkPredictionPhysicsState* InterpolatedState)
{
	npCheckSlow(Driver);
	npCheckSlow(InterpolatedState);
	npEnsure(Driver->IsSimulatingPhysics() == false);
	
	npEnsureSlow(InterpolatedState->Location.ContainsNaN() == false);
	npEnsureSlow(InterpolatedState->Rotation.ContainsNaN() == false);

	Driver->SetWorldLocationAndRotation(InterpolatedState->Location, InterpolatedState->Rotation, false);
}

#endif // WITH_CHAOS
