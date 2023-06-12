// Copyright Epic Games, Inc. All Rights Reserved.

#include "EngineAnalytics.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

class FNNECoreModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddLambda([]() {
			if (FEngineAnalytics::IsAvailable())
			{
				TArray<FAnalyticsEventAttribute> Attributes = MakeAnalyticsEventAttributeArray(
					TEXT("PlatformName"), UGameplayStatics::GetPlatformName(),
					TEXT("ModuleName"), TEXT("NNE")
				);
				FEngineAnalytics::GetProvider().RecordEvent(TEXT("NeuralNetworkEngine.StartupModule"), Attributes);
			}
		});
	}

	virtual void ShutdownModule() override
	{
	}

};

IMPLEMENT_MODULE(FNNECoreModule, NNECore)
