// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothModule.h"
#include "ChaosCloth/ChaosClothPrivate.h"
#include "ChaosCloth/ChaosClothingSimulationFactory.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

//////////////////////////////////////////////////////////////////////////
// FChaosClothModule

class FChaosClothModule : public IChaosClothModuleInterface, public IClothingSimulationFactoryClassProvider
{
  public:
    virtual void StartupModule() override
    {
        check(GConfig);
		IModularFeatures::Get().RegisterModularFeature(IClothingSimulationFactoryClassProvider::FeatureName, this);
    }

    virtual void ShutdownModule() override
    {
		IModularFeatures::Get().UnregisterModularFeature(IClothingSimulationFactoryClassProvider::FeatureName, this);
    }

	TSubclassOf<UClothingSimulationFactory> GetClothingSimulationFactoryClass() const override
	{
		return UChaosClothingSimulationFactory::StaticClass();
	}
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FChaosClothModule, ChaosCloth);
DEFINE_LOG_CATEGORY(LogChaosCloth);
