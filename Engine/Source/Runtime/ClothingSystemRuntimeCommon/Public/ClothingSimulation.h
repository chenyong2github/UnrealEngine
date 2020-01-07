// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothingSimulationInterface.h"
#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

class USkeletalMeshComponent;

/** Base simulation data that just about every simulation would need. */
class CLOTHINGSYSTEMRUNTIMECOMMON_API FClothingSimulationContextCommon : public IClothingSimulationContext
{
public:
	FClothingSimulationContextCommon();
	virtual ~FClothingSimulationContextCommon() override;

	// Fill this context using the given skeletal mesh component
	virtual void Fill(const USkeletalMeshComponent* InComponent, float InDeltaSeconds, float InMaxPhysicsDelta);

protected:
	// Default fill behavior as expected to be used by every simulation
	virtual void FillBoneTransforms(const USkeletalMeshComponent* InComponent);
	virtual void FillRefToLocals(const USkeletalMeshComponent* InComponent);
	virtual void FillComponentToWorld(const USkeletalMeshComponent* InComponent);
	virtual void FillWorldGravity(const USkeletalMeshComponent* InComponent);
	virtual void FillWindVelocity(const USkeletalMeshComponent* InComponent);
	virtual void FillDeltaSeconds(float InDeltaSeconds, float InMaxPhysicsDelta);

	// Set the wind velocity and return the wind adaptation if required
	float SetWindFromComponent(const USkeletalMeshComponent* Component);

public:
	// World space bone transforms of the owning component
	TArray<FTransform> BoneTransforms;

	// Ref to local matrices from the owning component (for skinning fixed verts)
	TArray<FMatrix> RefToLocals;

	// Component to world transform of the owning component
	FTransform ComponentToWorld;

	// Gravity extracted from the world
	FVector WorldGravity;

	// Wind velocity at the component location
	FVector WindVelocity;

	// Delta for this tick
	float DeltaSeconds;
};

// Base simulation to fill in common data for the base context
class CLOTHINGSYSTEMRUNTIMECOMMON_API FClothingSimulationCommon : public IClothingSimulation
{
public:
	FClothingSimulationCommon();
	virtual ~FClothingSimulationCommon();

protected:
	/** Fills in the base data for a clothing simulation */
	virtual void FillContext(USkeletalMeshComponent* InComponent, float InDeltaTime, IClothingSimulationContext* InOutContext) override;

private:
	/** Maximum physics time, incoming deltas will be clamped down to this value on long frames */
	float MaxPhysicsDelta;
};
