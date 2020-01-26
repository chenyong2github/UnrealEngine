// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothingSimulationFactoryNv.h"

#if WITH_NVCLOTH
#include "UObject/Package.h"
#include "ClothPhysicalMeshData.h"  // For EWeightMapTargetCommon
#include "ClothingSimulationNv.h"
#include "ClothingSimulationInteractorNv.h"
#endif

IClothingSimulation* UClothingSimulationFactoryNv::CreateSimulation()
{
#if WITH_NVCLOTH
	return new FClothingSimulationNv();
#else
	return nullptr;
#endif
}

void UClothingSimulationFactoryNv::DestroySimulation(IClothingSimulation* InSimulation)
{
#if WITH_NVCLOTH
	delete InSimulation;
#endif
}

bool UClothingSimulationFactoryNv::SupportsAsset(UClothingAssetBase* InAsset)
{
#if WITH_NVCLOTH
	return true;
#else
	return false;
#endif
}

bool UClothingSimulationFactoryNv::SupportsRuntimeInteraction()
{
	return true;
}

UClothingSimulationInteractor* UClothingSimulationFactoryNv::CreateInteractor()
{
#if WITH_NVCLOTH
	return NewObject<UClothingSimulationInteractorNv>(GetTransientPackage());
#else
	return nullptr;
#endif
}

TArrayView<const TSubclassOf<UClothConfigBase>> UClothingSimulationFactoryNv::GetClothConfigClasses() const
{
#if WITH_NVCLOTH
	static const TArray<TSubclassOf<UClothConfigBase>> ClothConfigClasses(
		{
			TSubclassOf<UClothConfigBase>(UClothConfigNv::StaticClass())
		});
	return ClothConfigClasses;
#else
	return TArrayView<const TSubclassOf<UClothConfigBase>>();
#endif
}

const UEnum* UClothingSimulationFactoryNv::GetWeightMapTargetEnum() const
{
#if WITH_NVCLOTH
	return StaticEnum<EWeightMapTargetCommon>();
#else
	return nullptr;
#endif
}
