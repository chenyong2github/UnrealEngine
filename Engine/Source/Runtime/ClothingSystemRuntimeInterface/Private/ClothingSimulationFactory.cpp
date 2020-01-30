// Copyright Epic Games, Inc. All Rights Reserved.

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
#else
		TEXT(""),  // otherwise it's none
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

	const TArray<IClothingSimulationFactoryClassProvider*> ClassProviders = IModularFeatures::Get().GetModularFeatureImplementations<IClothingSimulationFactoryClassProvider>(IClothingSimulationFactoryClassProvider::FeatureName);
	for (const auto& ClassProvider : ClassProviders)
	{
		if (ClassProvider)
		{
			const TSubclassOf<UClothingSimulationFactory> ClothingSimulationFactoryClass = ClassProvider->GetClothingSimulationFactoryClass();
			if (ClothingSimulationFactoryClass.Get() != nullptr)
			{
				// Always set the default to the last non null factory class (in case the search for the cvar doesn't yield any results)
				DefaultClothingSimulationFactoryClass = ClothingSimulationFactoryClass;

				// Early exit if the cvar string matches
				if (ClothingSimulationFactoryClass->GetName() == DefaultClothingSimulationFactoryClassName)
				{
					break;
				}
			}
		}
	}
	return DefaultClothingSimulationFactoryClass;
}
