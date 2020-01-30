// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ClothingSimulationInteractor.h"
#include "ChaosClothingSimulation.h"
#include "ChaosClothingSimulationInteractor.generated.h"

// Command signature for handling synced command buffer
DECLARE_DELEGATE_TwoParams(ChaosClothInteractorCommand, Chaos::ClothingSimulation*, Chaos::ClothingSimulationContext*)

UCLASS(BlueprintType)
class CHAOSCLOTH_API UChaosClothingSimulationInteractor : public UClothingSimulationInteractor
{
	GENERATED_BODY()
public:

	// UClothingSimulationInteractor Interface
	virtual void PhysicsAssetUpdated() override;
	virtual void ClothConfigUpdated() override;
	virtual void Sync(IClothingSimulation* InSimulation, IClothingSimulationContext* InContext) override;
	virtual void SetAnimDriveSpringStiffness(float InStiffness) override;
	virtual void EnableGravityOverride(const FVector& InVector) override;
	virtual void DisableGravityOverride() override;
	//////////////////////////////////////////////////////////////////////////

private:

	// Command queue processed when we hit a sync
	TArray<ChaosClothInteractorCommand> Commands;
};
