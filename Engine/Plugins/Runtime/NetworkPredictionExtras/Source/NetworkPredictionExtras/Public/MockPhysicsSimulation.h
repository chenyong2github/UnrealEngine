// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "Misc/StringBuilder.h"
#include "NetworkPredictionStateTypes.h"
#include "NetworkPredictionTickState.h"
#include "NetworkPredictionSimulation.h"
#include "NetworkPredictionReplicationProxy.h"
#include "PhysicsInterfaceDeclaresCore.h"

#include "MockPhysicsSimulation.generated.h"

// -------------------------------------------------------------------------------------------------------------------------------
// Mock Physics Simulation
//	This is an example of NP Sim + Physics working together. The NP sim is super simple: it applies forces to the physics body 
//	based on InputCmd. In this example there is no Sync state, mainly to illustrate that it is actually optional. That is not
//	a requirement of physics based NP sims.
// -------------------------------------------------------------------------------------------------------------------------------


// Making this a blueprint type to that it can be filled out by BPs.
USTRUCT(BlueprintType)
struct FMockPhysicsInputCmd
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Input)
	FVector MovementInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Input)
	bool bJumpedPressed  = false;

	FMockPhysicsInputCmd()
		: MovementInput(ForceInitToZero)
	{ }

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << MovementInput;
		P.Ar << bJumpedPressed;
	}

	void ToString(FAnsiStringBuilderBase& Out) const
	{
		Out.Appendf("MovementInput: X=%.2f Y=%.2f Z=%.2f\n", MovementInput.X, MovementInput.Y, MovementInput.Z);
		Out.Appendf("bJumpedPressed: %d\n", bJumpedPressed);
	}
};

struct FMockPhysicsAuxState
{
	float ForceMultiplier = 1.f;
	int32 JumpCooldownTime = 0; // can't jump again until sim time has passed this

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << ForceMultiplier;
		P.Ar << JumpCooldownTime;
	}
	void ToString(FAnsiStringBuilderBase& Out) const
	{
		Out.Appendf("ForceMultiplier: %.2f", ForceMultiplier);
		Out.Appendf("JumpCooldownTime: %d", JumpCooldownTime);
	}

	void Interpolate(const FMockPhysicsAuxState* From, const FMockPhysicsAuxState* To, float PCT)
	{
		ForceMultiplier = FMath::Lerp(From->ForceMultiplier, To->ForceMultiplier, PCT);
	}

	bool ShouldReconcile(const FMockPhysicsAuxState& AuthorityState) const
	{
		return AuthorityState.ForceMultiplier != ForceMultiplier || AuthorityState.JumpCooldownTime != JumpCooldownTime;
	}
};

using MockPhysicsStateTypes = TNetworkPredictionStateTypes<FMockPhysicsInputCmd, void, FMockPhysicsAuxState>;

class FMockPhysicsSimulation
{
public:
	
	void SimulationTick(const FNetSimTimeStep& TimeStep, const TNetSimInput<MockPhysicsStateTypes>& Input, const TNetSimOutput<MockPhysicsStateTypes>& Output);
	
	FPhysicsActorHandle PhysicsActorHandle;
};