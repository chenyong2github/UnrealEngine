// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothingSimulationInteractor.h"

void UChaosClothingSimulationInteractor::PhysicsAssetUpdated()
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([](Chaos::ClothingSimulation* InSimulation, Chaos::ClothingSimulationContext* InContext)
	{
		InSimulation->RefreshPhysicsAsset();
	}));

	MarkDirty();
}

void UChaosClothingSimulationInteractor::ClothConfigUpdated()
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([](Chaos::ClothingSimulation* InSimulation, Chaos::ClothingSimulationContext* InContext)
	{
		InSimulation->RefreshClothConfig();
	}));

	MarkDirty();
}

void UChaosClothingSimulationInteractor::Sync(IClothingSimulation* InSimulation, IClothingSimulationContext* InContext)
{
	check(InSimulation);
	check(InContext);

	Chaos::ClothingSimulation* ChaosSim = static_cast<Chaos::ClothingSimulation*>(InSimulation);
	Chaos::ClothingSimulationContext* ChaosContext = static_cast<Chaos::ClothingSimulationContext*>(InContext);

	for(ChaosClothInteractorCommand& Command : Commands)
	{
		Command.Execute(ChaosSim, ChaosContext);
	}
	Commands.Reset();
	bDirty = false;
}

void UChaosClothingSimulationInteractor::SetAnimDriveSpringStiffness(float InStiffness)
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([InStiffness](Chaos::ClothingSimulation* InSimulation, Chaos::ClothingSimulationContext* InContext)
	{
		InSimulation->SetAnimDriveSpringStiffness(InStiffness);
	}));

	MarkDirty();
}

void UChaosClothingSimulationInteractor::EnableGravityOverride(const FVector& InVector)
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([InVector](Chaos::ClothingSimulation* InSimulation, Chaos::ClothingSimulationContext* InContext)
	{
		InSimulation->SetGravityOverride(InVector);
	}));

	MarkDirty();
}

void UChaosClothingSimulationInteractor::DisableGravityOverride()
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([](Chaos::ClothingSimulation* InSimulation, Chaos::ClothingSimulationContext* InContext)
	{
		InSimulation->DisableGravityOverride();
	}));

	MarkDirty();
}

