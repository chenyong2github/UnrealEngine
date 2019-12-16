// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ClothingSimulationFactory.h"
#include "HAL/IConsoleManager.h"
#include "Features/IModularFeatures.h"

const FName IClothingSimulationFactoryClassProvider::FeatureName = TEXT("ClothingSimulationFactoryClassProvider");

namespace ClothingSimulationFactoryConsoleVariables
{
	TAutoConsoleVariable<FString> CVarDefaultClothingSimulationFactoryClass(
		TEXT("p.Cloth.DefaultClothingSimulationFactoryClass"),
#if WITH_CHAOS_CLOTHING
		TEXT("ChaosClothingSimulationFactory"),  // Chaos is the default provider when Chaos Cloth is enabled
#elif WITH_APEX_CLOTHING
		TEXT("ClothingSimulationFactoryNv"),  // otherwise it's nv cloth
#endif
		TEXT("The class name of the default clothing simulation factory.\n")
		TEXT("Known providers are:\n")
#if WITH_CHAOS_CLOTHING
		TEXT("ChaosClothingSimulationFactory\n")
#endif
#if WITH_APEX_CLOTHING
		TEXT("ClothingSimulationFactoryNv\n")
#endif
		, ECVF_Cheat);
}

TSubclassOf<class UClothingSimulationFactory> UClothingSimulationFactory::GetDefaultClothingSimulationFactoryClass()
{
	TSubclassOf<UClothingSimulationFactory> DefaultClothingSimulationFactoryClass = nullptr;

	const FString DefaultClothingSimulationFactoryClassName = ClothingSimulationFactoryConsoleVariables::CVarDefaultClothingSimulationFactoryClass.GetValueOnAnyThread();

	TArray<IClothingSimulationFactoryClassProvider*> ClassProviders = IModularFeatures::Get().GetModularFeatureImplementations<IClothingSimulationFactoryClassProvider>(IClothingSimulationFactoryClassProvider::FeatureName);
	for (const auto& ClassProvider : ClassProviders)
	{
		if (ClassProvider)
		{
			DefaultClothingSimulationFactoryClass = ClassProvider->GetClothingSimulationFactoryClass();

			if (DefaultClothingSimulationFactoryClass->GetName() == DefaultClothingSimulationFactoryClassName)
			{
				break;
			}
		}
	}
	return DefaultClothingSimulationFactoryClass;
}
