// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ClothingSimulationFactory.h"
#include "Misc/CoreMiscDefines.h"

#include "ClothingSimulationFactoryNv.generated.h"

UCLASS()
class UE_DEPRECATED(5.1, "NvCloth simulation is no longer supported, UChaosClothingSimulationFactory should be used going forward.") CLOTHINGSYSTEMRUNTIMENV_API UClothingSimulationFactoryNv final : public UClothingSimulationFactory
{
	GENERATED_BODY()
public:

	virtual IClothingSimulation* CreateSimulation() override;
	virtual void DestroySimulation(IClothingSimulation* InSimulation) override;
	virtual bool SupportsAsset(UClothingAssetBase* InAsset) override;

	virtual bool SupportsRuntimeInteraction() override;
	virtual UClothingSimulationInteractor* CreateInteractor() override;

	virtual TArrayView<const TSubclassOf<UClothConfigBase>> GetClothConfigClasses() const override;
	virtual const UEnum* GetWeightMapTargetEnum() const override;
};
