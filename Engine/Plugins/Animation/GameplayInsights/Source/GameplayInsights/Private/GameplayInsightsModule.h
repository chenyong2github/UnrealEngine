// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "GameplayTraceModule.h"
#include "GameplayTimingViewExtender.h"
#include "Framework/Docking/TabManager.h"
#include "IGameplayInsightsModule.h"

struct FInsightsMajorTabExtender;

namespace Trace 
{
	class FStoreService;
	class FStoreClient;
}

class FGameplayInsightsModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// Spawn a document tab
	TSharedRef<SDockTab> SpawnTimingProfilerDocumentTab(const FTabManager::FSearchPreference& InSearchPreference);

protected:
	void RegisterTimingProfilerLayoutExtensions(FInsightsMajorTabExtender& InOutExtender);
#if WITH_EDITOR
	void RegisterMenus();
#endif

private:
	FGameplayTraceModule GameplayTraceModule;

	FGameplayTimingViewExtender GameplayTimingViewExtender;

	FDelegateHandle TickerHandle;

#if WITH_EDITOR
	FDelegateHandle CustomDebugObjectHandle;
#endif

	TSharedPtr<Trace::FStoreService> StoreService;
	TWeakPtr<FTabManager> WeakTimingProfilerTabManager;
};
