// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ClothingSimulation.h"
#include "ClothingSimulationFactory.h"

#include "ClothingSimulationFactoryNv.generated.h"

UCLASS()
class CLOTHINGSYSTEMRUNTIMENV_API UClothingSimulationFactoryNv final : public UClothingSimulationFactory
{
	GENERATED_BODY()
public:

	virtual IClothingSimulation* CreateSimulation() override;
	virtual void DestroySimulation(IClothingSimulation* InSimulation) override;
	virtual bool SupportsAsset(UClothingAssetBase* InAsset) override;

	virtual bool SupportsRuntimeInteraction() override;
	virtual UClothingSimulationInteractor* CreateInteractor() override;
};