// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Features/IModularFeatures.h"
#include "GameplayTraceModule.h"
#include "GameplayTimingViewExtender.h"
#include "Insights/ITimingViewExtender.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "Framework/Multibox/MultiboxBuilder.h"

#if WITH_ENGINE
#include "Engine/Engine.h"
#endif

class FGameplayInsightsModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &GameplayTraceModule);
		IModularFeatures::Get().RegisterModularFeature(Insights::TimingViewExtenderFeatureName, &GameplayTimingViewExtender);


		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda([this]()
		{
#if WITH_ENGINE
			if(GEngine)
			{
				for(const FWorldContext& WorldContext : GEngine->GetWorldContexts())
				{
					GameplayTimingViewExtender.AddVisualizerWorld(WorldContext.World());
				}

				OnWorldAddedHandle = GEngine->OnWorldAdded().AddRaw(this, &FGameplayInsightsModule::HandleWorldAdded);
			}
#endif
		});

		TickerHandle = FTicker::GetCoreTicker().AddTicker(TEXT("GameplayInsights"), 0.0f, [this](float DeltaTime)
		{
			GameplayTimingViewExtender.TickVisualizers(DeltaTime);
			return true;
		});
	}

	virtual void ShutdownModule() override
	{
		FTicker::GetCoreTicker().RemoveTicker(TickerHandle);

		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);

#if WITH_ENGINE
		if(GEngine)
		{
			GEngine->OnWorldAdded().Remove(OnWorldAddedHandle);
		}
#endif

		IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &GameplayTraceModule);
		IModularFeatures::Get().UnregisterModularFeature(Insights::TimingViewExtenderFeatureName, &GameplayTimingViewExtender);
	}

#if WITH_ENGINE
	void HandleWorldAdded(UWorld* InWorld)
	{
		if (InWorld->WorldType == EWorldType::Editor ||
			InWorld->WorldType == EWorldType::PIE ||
			InWorld->WorldType == EWorldType::Game)
		{
			GameplayTimingViewExtender.AddVisualizerWorld(InWorld);
		}
	}
#endif

	FGameplayTraceModule GameplayTraceModule;
	FGameplayTimingViewExtender GameplayTimingViewExtender;

	FDelegateHandle OnWorldAddedHandle;
	FDelegateHandle TickerHandle;
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FGameplayInsightsModule, GameplayInsights);
