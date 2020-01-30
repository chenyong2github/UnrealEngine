// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ClothingSimulationInteractor.h"
#include "ClothingSimulationNv.h"
#include "ClothingSimulationInteractorNv.generated.h"

class FClothingSimulationNv;
class FClothingSimulationContextNv;

// Command signature for handling synced command buffer
DECLARE_DELEGATE_TwoParams(NvInteractorCommand, FClothingSimulationNv*, FClothingSimulationContextNv*)

UCLASS(BlueprintType)
class CLOTHINGSYSTEMRUNTIMENV_API UClothingSimulationInteractorNv : public UClothingSimulationInteractor
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

	// Set the stiffness of the resistive damping force for anim drive
	UFUNCTION(BlueprintCallable, Category=ClothingSimulation)
	void SetAnimDriveDamperStiffness(float InStiffness);

private:

	// Command queue processed when we hit a sync
	TArray<NvInteractorCommand> Commands;
};
