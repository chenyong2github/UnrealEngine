// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothingSimulationFactory.h"
#include "ChaosCloth/ChaosClothingSimulation.h"

#include "Assets/ClothingAsset.h"

IClothingSimulation* UChaosClothingSimulationFactory::CreateSimulation()
{
	IClothingSimulation* Simulation = new Chaos::ClothingSimulation();
	return Simulation;
}

void UChaosClothingSimulationFactory::DestroySimulation(IClothingSimulation* InSimulation)
{
    delete InSimulation;
}

bool UChaosClothingSimulationFactory::SupportsAsset(UClothingAssetBase* InAsset)
{
    return true;
}

bool UChaosClothingSimulationFactory::SupportsRuntimeInteraction()
{
    return false;
}

UClothingSimulationInteractor* UChaosClothingSimulationFactory::CreateInteractor()
{
	return nullptr;
}