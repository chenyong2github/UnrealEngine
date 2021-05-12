// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ClothingSimulationInteractor.h"
#include "ChaosClothingSimulationInteractor.generated.h"

namespace Chaos
{
	class FClothingSimulation;
	class FClothingSimulationCloth;
}
class FClothingSimulationContextCommon;

// Command signature for handling synced command buffer
DECLARE_DELEGATE_OneParam(FChaosClothingInteractorCommand, Chaos::FClothingSimulationCloth*)
DECLARE_DELEGATE_TwoParams(FChaosClothingSimulationInteractorCommand, Chaos::FClothingSimulation*, FClothingSimulationContextCommon*)

UCLASS(BlueprintType)
class CHAOSCLOTH_API UChaosClothingInteractor : public UClothingInteractor
{
	GENERATED_BODY()

public:
	virtual void Sync(IClothingSimulation* Simulation);

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Edge Bending Area Stiffness"))
	void SetMaterialLinear(float EdgeStiffness = 1.f, float BendingStiffness = 1.f, float AreaStiffness = 1.f);

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Tether Stiffness"))
	void SetLongRangeAttachmentLinear(float TetherStiffness = 1.f);

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Tether Stiffness"))
	void SetLongRangeAttachment(FVector2D TetherStiffness = FVector2D(0.f, 1.f));

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Self Thickness Friction Coefficient"))
	void SetCollision(float CollisionThickness = 1.f, float FrictionCoefficient = 0.8f, bool bUseCCD = false, float SelfCollisionThickness = 2.f);

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Coefficient"))
	void SetDamping(float DampingCoefficient = 0.01f);

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Wind Drag Lift Coefficient"))
	void SetAerodynamics(float DragCoefficient = 0.07f, float LiftCoefficient = 0.035f, FVector WindVelocity = FVector(0.f, 0.f, 0.f));

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Scale Override"))
	void SetGravity(float GravityScale = 1.f, bool bIsGravityOverridden = false, FVector GravityOverride = FVector(0.f, 0.f, -981.f));

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Stiffness"))
	void SetAnimDriveLinear(float AnimDriveStiffness = 0.f);

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Stiffness Damping"))
	void SetAnimDrive(FVector2D AnimDriveStiffness = FVector2D(0.f, 1.f), FVector2D AnimDriveDamping = FVector2D(0.f, 1.f));

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos Linear Angular Velocity Scale"))
	void SetVelocityScale(FVector LinearVelocityScale = FVector(0.75f, 0.75f, 0.75f), float AngularVelocityScale = 0.75f, float FictitiousAngularScale = 1.f);

	UFUNCTION(BlueprintCallable, Category = ClothingSimulation, Meta = (Keywords = "Chaos"))
	void ResetAndTeleport(bool bReset = false, bool bTeleport = false);

private:
	// Cloth command queue processed when we hit a sync
	TArray<FChaosClothingInteractorCommand> Commands;
};

UCLASS(BlueprintType)
class CHAOSCLOTH_API UChaosClothingSimulationInteractor : public UClothingSimulationInteractor
{
	GENERATED_BODY()
public:
	// UClothingSimulationInteractor interface
	virtual void Sync(IClothingSimulation* InSimulation, IClothingSimulationContext* InContext) override;

	virtual void PhysicsAssetUpdated() override;
	virtual void ClothConfigUpdated() override;
	virtual void SetAnimDriveSpringStiffness(float Stiffness) override;
	virtual void EnableGravityOverride(const FVector& Gravity) override;
	virtual void DisableGravityOverride() override;
	virtual void SetNumIterations(int32 NumIterations = 2) override;
	virtual void SetNumSubsteps(int32 NumSubsteps = 1) override;

protected:
	virtual UClothingInteractor* CreateClothingInteractor() override;

private:
	// Simulation command queue processed when we hit a sync
	TArray<FChaosClothingSimulationInteractorCommand> Commands;
};
