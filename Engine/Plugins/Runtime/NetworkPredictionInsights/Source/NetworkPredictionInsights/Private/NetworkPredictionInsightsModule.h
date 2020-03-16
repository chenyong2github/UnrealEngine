// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "NetworkPredictionTraceModule.h"
#include "Framework/Docking/TabManager.h"
#include "INetworkPredictionInsightsModule.h"

struct FInsightsMajorTabExtender;

namespace Trace 
{
	class FStoreService;
	class FStoreClient;
}

class FNetworkPredictionInsightsModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FNetworkPredictionTraceModule NetworkPredictionTraceModule;

	FDelegateHandle TickerHandle;
	FDelegateHandle StoreServiceHandle;

	TSharedPtr<Trace::FStoreService> StoreService;
	//TWeakPtr<FTabManager> WeakTimingProfilerTabManager;

	static const FName InsightsTabName;	
};
