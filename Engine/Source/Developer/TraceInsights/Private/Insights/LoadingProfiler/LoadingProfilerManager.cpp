// Copyright Epic Games, Inc. All Rights Reserved.

#include "LoadingProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/LoadTimeProfiler.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

// Insights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/LoadingProfiler/Widgets/SLoadingProfilerWindow.h"
#include "Insights/Table/Widgets/STableTreeView.h"
#include "Insights/Widgets/STimingView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "LoadingProfilerManager"

DEFINE_LOG_CATEGORY(LoadingProfiler);

TSharedPtr<FLoadingProfilerManager> FLoadingProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FLoadingProfilerManager> FLoadingProfilerManager::Get()
{
	return FLoadingProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FLoadingProfilerManager> FLoadingProfilerManager::CreateInstance()
{
	ensure(!FLoadingProfilerManager::Instance.IsValid());
	if (FLoadingProfilerManager::Instance.IsValid())
	{
		FLoadingProfilerManager::Instance.Reset();
	}

	FLoadingProfilerManager::Instance = MakeShared<FLoadingProfilerManager>(FInsightsManager::Get()->GetCommandList());

	return FLoadingProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLoadingProfilerManager::FLoadingProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: bIsInitialized(false)
	, bIsAvailable(false)
	, AvailabilityCheckNextTimestamp(0)
	, AvailabilityCheckWaitTimeSec(1.0)
	, CommandList(InCommandList)
	, ActionManager(this)
	, ProfilerWindow(nullptr)
	, bIsTimingViewVisible(false)
	, bIsEventAggregationTreeViewVisible(false)
	, bIsObjectTypeAggregationTreeViewVisible(false)
	, bIsPackageDetailsTreeViewVisible(false)
	, bIsExportDetailsTreeViewVisible(false)
	, bIsRequestsTreeViewVisible(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::Initialize(IUnrealInsightsModule& InsightsModule)
{
	ensure(!bIsInitialized);
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;

	// Register tick functions.
	OnTick = FTickerDelegate::CreateSP(this, &FLoadingProfilerManager::Tick);
	OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

	FLoadingProfilerCommands::Register();
	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}
	bIsInitialized = false;

	FLoadingProfilerCommands::Unregister();

	// Unregister tick function.
	FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);

	FLoadingProfilerManager::Instance.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLoadingProfilerManager::~FLoadingProfilerManager()
{
	ensure(!bIsInitialized);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::BindCommands()
{
	ActionManager.Map_ToggleTimingViewVisibility_Global();
	ActionManager.Map_ToggleEventAggregationTreeViewVisibility_Global();
	ActionManager.Map_ToggleObjectTypeAggregationTreeViewVisibility_Global();
	ActionManager.Map_TogglePackageDetailsTreeViewVisibility_Global();
	ActionManager.Map_ToggleExportDetailsTreeViewVisibility_Global();
	ActionManager.Map_ToggleRequestsTreeViewVisibility_Global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::RegisterMajorTabs(IUnrealInsightsModule& InsightsModule)
{
	const FInsightsMajorTabConfig& Config = InsightsModule.FindMajorTabConfig(FInsightsManagerTabs::LoadingProfilerTabId);

	if (Config.bIsAvailable)
	{
		// Register tab spawner for the Asset Loading Insights.
		FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId,
			FOnSpawnTab::CreateRaw(this, &FLoadingProfilerManager::SpawnTab), FCanSpawnTab::CreateRaw(this, &FLoadingProfilerManager::CanSpawnTab))
			.SetDisplayName(Config.TabLabel.IsSet() ? Config.TabLabel.GetValue() : LOCTEXT("LoadingProfilerTabTitle", "Asset Loading Insights"))
			.SetTooltipText(Config.TabTooltip.IsSet() ? Config.TabTooltip.GetValue() : LOCTEXT("LoadingProfilerTooltipText", "Open the Asset Loading Insights tab."))
			.SetIcon(Config.TabIcon.IsSet() ? Config.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "LoadingProfiler.Icon.Small"));

		TSharedRef<FWorkspaceItem> Group = Config.WorkspaceGroup.IsValid() ? Config.WorkspaceGroup.ToSharedRef() : FInsightsManager::Get()->GetInsightsMenuBuilder()->GetInsightsToolsGroup();
		TabSpawnerEntry.SetGroup(Group);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::UnregisterMajorTabs()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FLoadingProfilerManager::SpawnTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle I/O profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FLoadingProfilerManager::OnTabClosed));

	// Create the SLoadingProfilerWindow widget.
	TSharedRef<SLoadingProfilerWindow> Window = SNew(SLoadingProfilerWindow, DockTab, Args.GetOwnerWindow());
	DockTab->SetContent(Window);

	AssignProfilerWindow(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FLoadingProfilerManager::CanSpawnTab(const FSpawnTabArgs& Args) const
{
	return bIsAvailable;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::OnTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	RemoveProfilerWindow();

	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedRef<FUICommandList> FLoadingProfilerManager::GetCommandList() const
{
	return CommandList;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FLoadingProfilerCommands& FLoadingProfilerManager::GetCommands()
{
	return FLoadingProfilerCommands::Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLoadingProfilerActionManager& FLoadingProfilerManager::GetActionManager()
{
	return FLoadingProfilerManager::Instance->ActionManager;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FLoadingProfilerManager::Tick(float DeltaTime)
{
	if (!bIsAvailable)
	{
		// Check if session has Load Time events (to spawn the tab), but not too often.
		const uint64 Time = FPlatformTime::Cycles64();
		if (Time > AvailabilityCheckNextTimestamp)
		{
			AvailabilityCheckWaitTimeSec += 1.0; // increase wait time with 1s
			const uint64 WaitTime = static_cast<uint64>(AvailabilityCheckWaitTimeSec / FPlatformTime::GetSecondsPerCycle64());
			AvailabilityCheckNextTimestamp = Time + WaitTime;

			bool bIsProviderAvailable = false;

			TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			if (Session.IsValid())
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
				const Trace::ILoadTimeProfilerProvider* LoadTimeProfilerProvider = Trace::ReadLoadTimeProfilerProvider(*Session.Get());
				if (LoadTimeProfilerProvider)
				{
					bIsProviderAvailable = (LoadTimeProfilerProvider->GetTimelineCount() > 0);
				}
			}

			if (bIsProviderAvailable)
			{
				bIsAvailable = true;

				const FName& TabId = FInsightsManagerTabs::LoadingProfilerTabId;
				if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
				{
					FGlobalTabmanager::Get()->TryInvokeTab(TabId);
				}
			}
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::OnSessionChanged()
{
	bIsAvailable = false;
	AvailabilityCheckNextTimestamp = 0;
	AvailabilityCheckWaitTimeSec = 1.0;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::ShowHideTimingView(const bool bIsVisible)
{
	bIsTimingViewVisible = bIsVisible;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FLoadingProfilerTabs::TimingViewID, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::ShowHideEventAggregationTreeView(const bool bIsVisible)
{
	bIsEventAggregationTreeViewVisible = bIsVisible;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FLoadingProfilerTabs::EventAggregationTreeViewID, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::ShowHideObjectTypeAggregationTreeView(const bool bIsVisible)
{
	bIsObjectTypeAggregationTreeViewVisible = bIsVisible;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FLoadingProfilerTabs::ObjectTypeAggregationTreeViewID, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::ShowHidePackageDetailsTreeView(const bool bIsVisible)
{
	bIsPackageDetailsTreeViewVisible = bIsVisible;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FLoadingProfilerTabs::PackageDetailsTreeViewID, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::ShowHideExportDetailsTreeView(const bool bIsVisible)
{
	bIsExportDetailsTreeViewVisible = bIsVisible;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FLoadingProfilerTabs::ExportDetailsTreeViewID, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::ShowHideRequestsTreeView(const bool bIsVisible)
{
	bIsRequestsTreeViewVisible = bIsVisible;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FLoadingProfilerTabs::RequestsTreeViewID, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
