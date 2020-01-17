// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "GameplayTraceModule.h"
#include "GameplayTimingViewExtender.h"
#include "Framework/Docking/TabManager.h"

class FGameplayInsightsModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// Spawn a document tab
	TSharedRef<SDockTab> SpawnTimingProfilerDocumentTab(const FTabManager::FSearchPreference& InSearchPreference);

private:
	FGameplayTraceModule GameplayTraceModule;

	FGameplayTimingViewExtender GameplayTimingViewExtender;

	FDelegateHandle TickerHandle;

#if WITH_EDITOR
	FDelegateHandle CustomDebugObjectHandle;
#endif

	TWeakPtr<FTabManager> WeakTimingProfilerTabManager;
};