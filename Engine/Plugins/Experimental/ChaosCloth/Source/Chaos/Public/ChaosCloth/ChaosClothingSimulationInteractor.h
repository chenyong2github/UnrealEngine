// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ClothingSimulationInteractor.h"
#include "ChaosClothingSimulation.h"
#include "ChaosClothingSimulationInteractor.generated.h"

// Command signature for handling synced command buffer
DECLARE_DELEGATE_TwoParams(ChaosClothInteractorCommand, Chaos::FClothingSimulation*, FClothingSimulationContextCommon*)

UCLASS(BlueprintType)
class CHAOSCLOTH_API UChaosClothingSimulationInteractor : public UClothingSimulationInteractor
{
	GENERATED_BODY()
public:

	// UClothingSimulationInteractor interface
	virtual void PhysicsAssetUpdated() override;
	virtual void ClothConfigUpdated() override;
	virtual void Sync(IClothingSimulation* InSimulation, IClothingSimulationContext* InContext) override;
	virtual void SetAnimDriveSpringStiffness(float InStiffness) override;
	virtual void EnableGravityOverride(const FVector& InVector) override;
	virtual void DisableGravityOverride() override;

	virtual int32 GetNumCloths() const override { return NumCloths; }
	virtual int32 GetNumKinematicParticles() const override { return NumKinematicParticles; }
	virtual int32 GetNumDynamicParticles() const override { return NumDynamicParticles; }
	virtual int32 GetNumIterations() const override { return NumIterations; }
	virtual int32 GetNumSubsteps() const override { return NumSubsteps; }
	virtual float GetSimulationTime() const override { return SimulationTime; }
	// End of UClothingSimulationInteractor interface

private:

	// Command queue processed when we hit a sync
	TArray<ChaosClothInteractorCommand> Commands;

	// Simulation infos updated during the sync
	int32 NumCloths;
	int32 NumKinematicParticles;
	int32 NumDynamicParticles;
	int32 NumIterations;
	int32 NumSubsteps;
	float SimulationTime;
};
