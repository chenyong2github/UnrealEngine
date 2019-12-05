// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Features/IModularFeatures.h"
#include "GameplayTraceModule.h"
#include "GameplayTimingViewExtender.h"
#include "Insights/ITimingViewExtender.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "IAnimationBlueprintEditorModule.h"
#endif

#if WITH_ENGINE
#include "Engine/Engine.h"
#endif

#define LOCTEXT_NAMESPACE "GameplayInsightsModule"

class FGameplayInsightsModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &GameplayTraceModule);
		IModularFeatures::Get().RegisterModularFeature(Insights::TimingViewExtenderFeatureName, &GameplayTimingViewExtender);

		TickerHandle = FTicker::GetCoreTicker().AddTicker(TEXT("GameplayInsights"), 0.0f, [this](float DeltaTime)
		{
			GameplayTimingViewExtender.TickVisualizers(DeltaTime);
			return true;
		});

#if WITH_EDITOR
		IAnimationBlueprintEditorModule& AnimationBlueprintEditorModule = FModuleManager::LoadModuleChecked<IAnimationBlueprintEditorModule>("AnimationBlueprintEditor");
		CustomDebugObjectHandle = AnimationBlueprintEditorModule.OnGetCustomDebugObjects().AddLambda([this](const IAnimationBlueprintEditor& InAnimationBlueprintEditor, TArray<FCustomDebugObject>& OutDebugList)
		{
			GameplayTimingViewExtender.GetCustomDebugObjects(InAnimationBlueprintEditor, OutDebugList);
		});
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		IAnimationBlueprintEditorModule* AnimationBlueprintEditorModule = FModuleManager::GetModulePtr<IAnimationBlueprintEditorModule>("AnimationBlueprintEditor");
		if(AnimationBlueprintEditorModule)
		{
			AnimationBlueprintEditorModule->OnGetCustomDebugObjects().Remove(CustomDebugObjectHandle);
		}
#endif

		FTicker::GetCoreTicker().RemoveTicker(TickerHandle);

		IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &GameplayTraceModule);
		IModularFeatures::Get().UnregisterModularFeature(Insights::TimingViewExtenderFeatureName, &GameplayTimingViewExtender);
	}

	FGameplayTraceModule GameplayTraceModule;
	FGameplayTimingViewExtender GameplayTimingViewExtender;

	FDelegateHandle TickerHandle;

#if WITH_EDITOR
	FDelegateHandle CustomDebugObjectHandle;
#endif
};

IMPLEMENT_MODULE(FGameplayInsightsModule, GameplayInsights);

#undef LOCTEXT_NAMESPACE