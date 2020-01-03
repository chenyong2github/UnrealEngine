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
	//////////////////////////////////////////////////////////////////////////

	// Set the stiffness of the spring force for the animation drive
	UFUNCTION(BlueprintCallable, Category=ClothingSimulation)
	void SetAnimDriveSpringStiffness(float InStiffness);

	// Set a new gravity override and enable the override
	UFUNCTION(BlueprintCallable, Category=ClothingSimulation)
	void EnableGravityOverride(const FVector& InVector);

	// Disable any currently set gravity override
	UFUNCTION(BlueprintCallable, Category = ClothingSimulation)
	void DisableGravityOverride();

private:

	// Command queue processed when we hit a sync
	TArray<ChaosClothInteractorCommand> Commands;
};
