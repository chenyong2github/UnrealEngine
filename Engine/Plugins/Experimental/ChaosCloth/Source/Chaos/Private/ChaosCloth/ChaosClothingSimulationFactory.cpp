// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothingSimulationFactory.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "ChaosCloth/ChaosClothingSimulation.h"
#include "ChaosCloth/ChaosWeightMapTarget.h"
#include "ChaosCloth/ChaosClothingSimulationInteractor.h"

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
    return true;
}

UClothingSimulationInteractor* UChaosClothingSimulationFactory::CreateInteractor()
{
	return NewObject<UChaosClothingSimulationInteractor>(GetTransientPackage());
}

TSubclassOf<UClothConfigBase> UChaosClothingSimulationFactory::GetClothConfigClass() const
{
	return TSubclassOf<UClothConfigBase>(UChaosClothConfig::StaticClass());
}

const UEnum* UChaosClothingSimulationFactory::GetWeightMapTargetEnum() const
{
	return StaticEnum<EChaosWeightMapTarget>();
}
