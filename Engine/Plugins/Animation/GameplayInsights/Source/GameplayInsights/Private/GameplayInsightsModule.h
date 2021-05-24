// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "GameplayTraceModule.h"
#include "GameplayTimingViewExtender.h"
#include "Framework/Docking/TabManager.h"
#include "IGameplayInsightsModule.h"
#include "GameplayInsightsDebugViewCreator.h"

struct FInsightsMajorTabExtender;

namespace UE 
{
namespace Trace 
{
	class FStoreService;
	class FStoreClient;
}
}

class FGameplayInsightsModule : public IGameplayInsightsModule
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// IGameplayInsightsModule interface
	virtual IGameplayInsightsDebugViewCreator* GetDebugViewCreator() override { return &DebugViewCreator; }

#if WITH_EDITOR
	virtual void EnableObjectPropertyTrace(UObject* Object, bool bEnable = true) override;
	virtual bool IsObjectPropertyTraceEnabled(UObject* Object) override;
#endif

	// Spawn a document tab
	TSharedRef<SDockTab> SpawnTimingProfilerDocumentTab(const FTabManager::FSearchPreference& InSearchPreference);

protected:
	void RegisterTimingProfilerLayoutExtensions(FInsightsMajorTabExtender& InOutExtender);
#if WITH_EDITOR
	void RegisterMenus();
#endif

private:
	FGameplayInsightsDebugViewCreator DebugViewCreator;

	FGameplayTraceModule GameplayTraceModule;

	FGameplayTimingViewExtender GameplayTimingViewExtender;

	FDelegateHandle TickerHandle;

#if WITH_EDITOR
	FDelegateHandle CustomDebugObjectHandle;
#endif

	TSharedPtr<UE::Trace::FStoreService> StoreService;
	TWeakPtr<FTabManager> WeakTimingProfilerTabManager;
};
