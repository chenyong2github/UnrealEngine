// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "Units/RigUnit.h"
#include "RigUnit_WorldCollision.generated.h"

/**
 * Sweeps a sphere against the world and return the first blocking hit using a specific channel
 */
USTRUCT(meta=(DisplayName="Sphere Trace", Category="Collision", DocumentationPolicy = "Strict", Keywords="Sweep,Raytrace,Collision,Collide,Trace", Varying, NodeColor = "0.2 0.4 0.7"))
struct FRigUnit_SphereTraceWorld : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_SphereTraceWorld()
	: Start(EForceInit::ForceInitToZero)
	, End(EForceInit::ForceInitToZero)
	, Channel(ECC_Visibility)
	, Radius(5.f)
	, bHit(false)
	, HitLocation(EForceInit::ForceInitToZero)
	, HitNormal(0.f, 0.f, 1.f)
	{

	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** Start of the trace in rig / global space */
	UPROPERTY(meta = (Input))
	FVector Start;

	/** End of the trace in rig / global space */
	UPROPERTY(meta = (Input))
	FVector End;

	/** The 'channel' that this trace is in, used to determine which components to hit */
	UPROPERTY(meta = (Input))
	TEnumAsByte<ECollisionChannel> Channel;

	/** Radius of the sphere to use for sweeping / tracing */
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "100.0"))
	float Radius;

	/** Returns true if there was a hit */
	UPROPERTY(meta = (Output))
	bool bHit;

	/** Hit location in rig / global Space */
	UPROPERTY(meta = (Output))
	FVector HitLocation;
	
	/** Hit normal in rig / global Space */
	UPROPERTY(meta = (Output))
	FVector HitNormal;
};
