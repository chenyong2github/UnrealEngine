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
	virtual void Sync(IClothingSimulation* InSimulation, IClothingSimulationContext* InContext) override;

	virtual void PhysicsAssetUpdated() override;
	virtual void ClothConfigUpdated() override;
	virtual void SetAnimDriveSpringStiffness(float InStiffness) override;
	virtual void EnableGravityOverride(const FVector& InVector) override;
	virtual void DisableGravityOverride() override;

	// TODO: These new functions are currently unimplemented
	virtual void SetNumIterations(int32 /*NumIterations*/) override {}
	virtual void SetNumSubsteps(int32 /*NumSubsteps*/) override {}
	//////////////////////////////////////////////////////////////////////////

	// Set the stiffness of the resistive damping force for anim drive
	UFUNCTION(BlueprintCallable, Category=ClothingSimulation)
	void SetAnimDriveDamperStiffness(float InStiffness);

protected:

	virtual UClothingInteractor* CreateClothingInteractor() override { return nullptr; }

private:

	// Command queue processed when we hit a sync
	TArray<NvInteractorCommand> Commands;
};
