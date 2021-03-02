// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionInsightsModule.h"
#include "Features/IModularFeatures.h"
#include "Insights/ITimingViewExtender.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Insights/IUnrealInsightsModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "TraceServices/ITraceServicesModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Trace/StoreService.h"
#include "Trace/StoreClient.h"
#include "Stats/Stats.h"

#include "UI/SNPWindow.h"
#include "UI/NetworkPredictionInsightsManager.h"
#include "EditorStyleSet.h"

#if WITH_ENGINE
#include "Engine/Engine.h"
#endif


#define LOCTEXT_NAMESPACE "NetworkPredictionInsightsModule"

const FName NetworkPredictionInsightsTabs::DocumentTab("DocumentTab");

const FName FNetworkPredictionInsightsModule::InsightsTabName("NetworkPrediction");

void FNetworkPredictionInsightsModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &NetworkPredictionTraceModule);

	FNetworkPredictionInsightsManager::Initialize();
	IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

	// Auto spawn the Network Prediction Insights tab if we detect NP data
	// Only do this in Standalone UnrealInsights.exe. In Editor the user will select the NPI window manually.
	// (There is currently not way to extend the high level layout of unreal insights, e.g, the layout created in FTraceInsightsModule::CreateSessionBrowser)
	// (only SetUnrealInsightsLayoutIni which is extending the layouts of pre made individual tabs, not the the overall session layout)
	if (!GIsEditor)
	{
		TickerHandle = FTicker::GetCoreTicker().AddTicker(TEXT("NetworkPredictionInsights"), 0.0f, [&UnrealInsightsModule](float DeltaTime)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FNetworkPredictionInsightsModule_Tick);
			auto SessionPtr = UnrealInsightsModule.GetAnalysisSession();
			if (SessionPtr.IsValid())
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*SessionPtr.Get());
				if (const INetworkPredictionProvider* NetworkPredictionProvider = ReadNetworkPredictionProvider(*SessionPtr.Get()))
				{
					auto NetworkPredictionTraceVersion = NetworkPredictionProvider->GetNetworkPredictionTraceVersion();

					if (NetworkPredictionTraceVersion > 0)
					{
						static bool HasSpawnedTab = false;
						if (!HasSpawnedTab && FGlobalTabmanager::Get()->HasTabSpawner(FNetworkPredictionInsightsModule::InsightsTabName))
						{
							HasSpawnedTab = true;
							FGlobalTabmanager::Get()->TryInvokeTab(FNetworkPredictionInsightsModule::InsightsTabName);
						}
					}
				}
			}

			return true;
		});
	}

	// Actually register our tab spawner
	FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FNetworkPredictionInsightsModule::InsightsTabName,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args)
	{
		const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
			.TabRole(ETabRole::NomadTab);

		TSharedRef<SNPWindow> Window = SNew(SNPWindow, DockTab, Args.GetOwnerWindow());
		DockTab->SetContent(Window);
		return DockTab;
	}))
		.SetDisplayName(NSLOCTEXT("FNetworkPredictionInsightsModule", "NetworkPredictionTabTitle", "Network Prediction Insights"))
		.SetTooltipText(NSLOCTEXT("FNetworkPredictionInsightsModule", "FilteringTabTooltip", "Opens the Network Prediction Insights tab."))
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "ProfilerCommand.StatsProfiler.Small"));

	//TabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory());
	TabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	// -------------------------------------------------------------

#if WITH_EDITOR
	if (!IsRunningCommandlet())
	{
		// Conditionally create local store service after engine init (if someone doesn't beat us to it).
		// This is temp until a more formal local server is done by the insights system.
		StoreServiceHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]
		{
			IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
			if (!UnrealInsightsModule.GetStoreClient())
			{
				// Create the Store Service.
				FString StoreDir = FPaths::ProjectSavedDir() / TEXT("TraceSessions");
				Trace::FStoreService::FDesc StoreServiceDesc;
				StoreServiceDesc.StoreDir = *StoreDir;
				StoreServiceDesc.RecorderPort = 0; // Let system decide port
				StoreServiceDesc.ThreadCount = 2;
				StoreService = TSharedPtr<Trace::FStoreService>(Trace::FStoreService::Create(StoreServiceDesc));

				FCoreDelegates::OnPreExit.AddLambda([this]() {
					StoreService.Reset();
				});

				// Connect to our newly created store and setup the insights module
				ensure(UnrealInsightsModule.ConnectToStore(TEXT("localhost"), StoreService->GetPort()));
				Trace::SendTo(TEXT("localhost"), StoreService->GetRecorderPort());

				UnrealInsightsModule.CreateSessionViewer(false);
				UnrealInsightsModule.StartAnalysisForLastLiveSession();
			}
		});
	}
#endif
}

void FNetworkPredictionInsightsModule::ShutdownModule()
{
	if (StoreServiceHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(StoreServiceHandle);
	}

	FTicker::GetCoreTicker().RemoveTicker(TickerHandle);

	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &NetworkPredictionTraceModule);
}

IMPLEMENT_MODULE(FNetworkPredictionInsightsModule, NetworkPredictionInsights);

#undef LOCTEXT_NAMESPACE
