// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothingSimulationInteractor.h"

void UChaosClothingSimulationInteractor::PhysicsAssetUpdated()
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([](Chaos::FClothingSimulation* InSimulation, Chaos::FClothingSimulationContext* InContext)
	{
		InSimulation->RefreshPhysicsAsset();
	}));

	MarkDirty();
}

void UChaosClothingSimulationInteractor::ClothConfigUpdated()
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([](Chaos::FClothingSimulation* InSimulation, Chaos::FClothingSimulationContext* InContext)
	{
		InSimulation->RefreshClothConfig(InContext);
	}));

	MarkDirty();
}

void UChaosClothingSimulationInteractor::Sync(IClothingSimulation* InSimulation, IClothingSimulationContext* InContext)
{
	check(InSimulation);
	check(InContext);

	Chaos::FClothingSimulation* ChaosSim = static_cast<Chaos::FClothingSimulation*>(InSimulation);
	Chaos::FClothingSimulationContext* ChaosContext = static_cast<Chaos::FClothingSimulationContext*>(InContext);

	for(ChaosClothInteractorCommand& Command : Commands)
	{
		Command.Execute(ChaosSim, ChaosContext);
	}
	Commands.Reset();

	NumCloths = InSimulation->GetNumCloths();
	NumKinematicParticles = InSimulation->GetNumKinematicParticles();
	NumDynamicParticles = InSimulation->GetNumDynamicParticles();
	NumIterations = InSimulation->GetNumIterations();
	NumSubsteps = InSimulation->GetNumSubsteps();
	SimulationTime = InSimulation->GetSimulationTime();

	bDirty = false;
}

void UChaosClothingSimulationInteractor::SetAnimDriveSpringStiffness(float InStiffness)
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([InStiffness](Chaos::FClothingSimulation* InSimulation, Chaos::FClothingSimulationContext* InContext)
	{
		InSimulation->SetAnimDriveSpringStiffness(InStiffness);
	}));

	MarkDirty();
}

void UChaosClothingSimulationInteractor::EnableGravityOverride(const FVector& InVector)
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([InVector](Chaos::FClothingSimulation* InSimulation, Chaos::FClothingSimulationContext* InContext)
	{
		InSimulation->SetGravityOverride(InVector);
	}));

	MarkDirty();
}

void UChaosClothingSimulationInteractor::DisableGravityOverride()
{
	Commands.Add(ChaosClothInteractorCommand::CreateLambda([](Chaos::FClothingSimulation* InSimulation, Chaos::FClothingSimulationContext* InContext)
	{
		InSimulation->DisableGravityOverride();
	}));

	MarkDirty();
}

