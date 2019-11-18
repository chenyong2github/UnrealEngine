// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothingSimulationFactory.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "ChaosCloth/ChaosClothingSimulation.h"
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

UClothConfigBase* UChaosClothingSimulationFactory::CreateDefaultClothConfig(const FObjectInitializer& ObjectInitializer, UObject* Outer)
{
	return ObjectInitializer.CreateDefaultSubobject<UChaosClothConfig>(Outer, UChaosClothConfig::StaticClass()->GetFName());
}