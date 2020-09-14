// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimingProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

// Insights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/ViewModels/TimerButterflyAggregation.h"
#include "Insights/Widgets/SFrameTrack.h"
#include "Insights/Widgets/SLogView.h"
#include "Insights/Widgets/SStatsView.h"
#include "Insights/Widgets/STimersView.h"
#include "Insights/Widgets/STimerTreeView.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "TimingProfilerManager"

DEFINE_LOG_CATEGORY(TimingProfiler);

TSharedPtr<FTimingProfilerManager> FTimingProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTimingProfilerManager> FTimingProfilerManager::Get()
{
	return FTimingProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTimingProfilerManager> FTimingProfilerManager::CreateInstance()
{
	ensure(!FTimingProfilerManager::Instance.IsValid());
	if (FTimingProfilerManager::Instance.IsValid())
	{
		FTimingProfilerManager::Instance.Reset();
	}

	FTimingProfilerManager::Instance = MakeShared<FTimingProfilerManager>(FInsightsManager::Get()->GetCommandList());

	return FTimingProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingProfilerManager::FTimingProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: bIsInitialized(false)
	, bIsAvailable(false)
	, CommandList(InCommandList)
	, ActionManager(this)
	, ProfilerWindow(nullptr)
	, bIsFramesTrackVisible(false)
	, bIsTimingViewVisible(false)
	, bIsTimersViewVisible(false)
	, bIsCallersTreeViewVisible(false)
	, bIsCalleesTreeViewVisible(false)
	, bIsStatsCountersViewVisible(false)
	, bIsLogViewVisible(false)
	, SelectionStartTime(0.0)
	, SelectionEndTime(0.0)
	, SelectedTimerId(InvalidTimerId)
	, TimerButterflyAggregator(MakeShared<Insights::FTimerButterflyAggregator>())
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::Initialize(IUnrealInsightsModule& InsightsModule)
{
	ensure(!bIsInitialized);
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;

	UE_LOG(TimingProfiler, Log, TEXT("Initialize"));

	// Register tick functions.
	OnTick = FTickerDelegate::CreateSP(this, &FTimingProfilerManager::Tick);
	OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 0.0f);

	FTimingProfilerCommands::Register();
	BindCommands();

	FInsightsManager::Get()->GetSessionChangedEvent().AddSP(this, &FTimingProfilerManager::OnSessionChanged);
	OnSessionChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}
	bIsInitialized = false;

	FInsightsManager::Get()->GetSessionChangedEvent().RemoveAll(this);

	FTimingProfilerCommands::Unregister();

	// Unregister tick function.
	FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);

	FTimingProfilerManager::Instance.Reset();

	UE_LOG(TimingProfiler, Log, TEXT("Shutdown"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingProfilerManager::~FTimingProfilerManager()
{
	ensure(!bIsInitialized);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::BindCommands()
{
	ActionManager.Map_ToggleFramesTrackVisibility_Global();
	ActionManager.Map_ToggleTimingViewVisibility_Global();
	ActionManager.Map_ToggleTimersViewVisibility_Global();
	ActionManager.Map_ToggleCallersTreeViewVisibility_Global();
	ActionManager.Map_ToggleCalleesTreeViewVisibility_Global();
	ActionManager.Map_ToggleStatsCountersViewVisibility_Global();
	ActionManager.Map_ToggleLogViewVisibility_Global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::RegisterMajorTabs(IUnrealInsightsModule& InsightsModule)
{
	const FInsightsMajorTabConfig& Config = InsightsModule.FindMajorTabConfig(FInsightsManagerTabs::TimingProfilerTabId);
	if (Config.bIsAvailable)
	{
		// Register tab spawner for the Timing Insights.
		FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::TimingProfilerTabId,
			FOnSpawnTab::CreateRaw(this, &FTimingProfilerManager::SpawnTab), FCanSpawnTab::CreateRaw(this, &FTimingProfilerManager::CanSpawnTab))
			.SetDisplayName(Config.TabLabel.IsSet() ? Config.TabLabel.GetValue() : LOCTEXT("TimingProfilerTabTitle", "Timing Insights"))
			.SetTooltipText(Config.TabTooltip.IsSet() ? Config.TabTooltip.GetValue() : LOCTEXT("TimingProfilerTooltipText", "Open the Timing Insights tab."))
			.SetIcon(Config.TabIcon.IsSet() ? Config.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimingProfiler.Icon.Small"));

		TSharedRef<FWorkspaceItem> Group = Config.WorkspaceGroup.IsValid() ? Config.WorkspaceGroup.ToSharedRef() : FInsightsManager::Get()->GetInsightsMenuBuilder()->GetInsightsToolsGroup();
		TabSpawnerEntry.SetGroup(Group);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::UnregisterMajorTabs()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::TimingProfilerTabId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTimingProfilerManager::SpawnTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle Timing profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FTimingProfilerManager::OnTabClosed));

	// Create the STimingProfilerWindow widget.
	TSharedRef<STimingProfilerWindow> Window = SNew(STimingProfilerWindow, DockTab, Args.GetOwnerWindow());
	DockTab->SetContent(Window);

	AssignProfilerWindow(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingProfilerManager::CanSpawnTab(const FSpawnTabArgs& Args) const
{
#if !WITH_EDITOR
	return bIsAvailable;
#else
	return true;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::OnTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	RemoveProfilerWindow();

	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedRef<FUICommandList> FTimingProfilerManager::GetCommandList() const
{
	return CommandList;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FTimingProfilerCommands& FTimingProfilerManager::GetCommands()
{
	return FTimingProfilerCommands::Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingProfilerActionManager& FTimingProfilerManager::GetActionManager()
{
	return FTimingProfilerManager::Instance->ActionManager;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingProfilerManager::Tick(float DeltaTime)
{
	// Check if session has Timing events (to spawn the tab), but not too often.
	if (!bIsAvailable && AvailabilityCheck.Tick())
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			bIsAvailable = true;

#if !WITH_EDITOR
			const FName& TabId = FInsightsManagerTabs::TimingProfilerTabId;
			if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
			{
				UE_LOG(TimingProfiler, Log, TEXT("Opening the \"Timing Insights\" tab..."));
				FGlobalTabmanager::Get()->TryInvokeTab(TabId);
			}
#endif
		}
		else
		{
			// Do not check again until the next session changed event (see OnSessionChanged).
			AvailabilityCheck.Disable();
		}
	}

	TimerButterflyAggregator->Tick(FInsightsManager::Get()->GetSession(), 0.0f, DeltaTime, [this]() { FinishTimerButterflyAggregation(); });

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::FinishTimerButterflyAggregation()
{
	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd)
	{
		TSharedPtr<STimerTreeView> CallersTreeView = Wnd->GetCallersTreeView();
		if (CallersTreeView)
		{
			Trace::ITimingProfilerButterfly* TimingProfilerButterfly = TimerButterflyAggregator->GetResultButterfly();
			const Trace::FTimingProfilerButterflyNode& Callers = TimingProfilerButterfly->GenerateCallersTree(SelectedTimerId);
			CallersTreeView->SetTree(Callers);
		}

		TSharedPtr<STimerTreeView> CalleesTreeView = Wnd->GetCalleesTreeView();
		if (CalleesTreeView)
		{
			Trace::ITimingProfilerButterfly* TimingProfilerButterfly = TimerButterflyAggregator->GetResultButterfly();
			const Trace::FTimingProfilerButterflyNode& Callees = TimingProfilerButterfly->GenerateCalleesTree(SelectedTimerId);
			CalleesTreeView->SetTree(Callees);
		}
	}

	TimerButterflyAggregator->ResetResults();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::OnSessionChanged()
{
	UE_LOG(TimingProfiler, Log, TEXT("OnSessionChanged"));

	bIsAvailable = false;
	if (FInsightsManager::Get()->GetSession().IsValid())
	{
		AvailabilityCheck.Enable(0.0);
	}
	else
	{
		AvailabilityCheck.Disable();
	}

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->Reset();
	}

	SelectionStartTime = 0.0;
	SelectionEndTime = 0.0;
	SelectedTimerId = InvalidTimerId;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideFramesTrack(const bool bIsVisible)
{
	bIsFramesTrackVisible = bIsVisible;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::FramesTrackID, bIsFramesTrackVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideTimingView(const bool bIsVisible)
{
	bIsTimingViewVisible = bIsVisible;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::TimingViewID, bIsTimingViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideTimersView(const bool bIsVisible)
{
	bIsTimersViewVisible = bIsVisible;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::TimersID, bIsTimersViewVisible);

		if (bIsTimersViewVisible)
		{
			UpdateAggregatedTimerStats();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideCallersTreeView(const bool bIsVisible)
{
	bIsCallersTreeViewVisible = bIsVisible;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::CallersID, bIsCallersTreeViewVisible);

		if (bIsCallersTreeViewVisible)
		{
			UpdateCallersAndCallees();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideCalleesTreeView(const bool bIsVisible)
{
	bIsCalleesTreeViewVisible = bIsVisible;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::CalleesID, bIsCalleesTreeViewVisible);

		if (bIsCalleesTreeViewVisible)
		{
			UpdateCallersAndCallees();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideStatsCountersView(const bool bIsVisible)
{
	bIsStatsCountersViewVisible = bIsVisible;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::StatsCountersID, bIsStatsCountersViewVisible);

		if (bIsStatsCountersViewVisible)
		{
			UpdateAggregatedCounterStats();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideLogView(const bool bIsVisible)
{
	bIsLogViewVisible = bIsVisible;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::LogViewID, bIsLogViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::SetSelectedTimeRange(double InStartTime, double InEndTime)
{
	if (InStartTime != SelectionStartTime ||
		InEndTime != SelectionEndTime)
	{
		SelectionStartTime = InStartTime;
		SelectionEndTime = InEndTime;

		UpdateCallersAndCallees();
		UpdateAggregatedTimerStats();
		UpdateAggregatedCounterStats();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimerNodePtr FTimingProfilerManager::GetTimerNode(uint32 InTimerId) const
{
	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd)
	{
		TSharedPtr<STimersView> TimersView = Wnd->GetTimersView();
		if (TimersView)
		{
			FTimerNodePtr TimerNodePtr = TimersView->GetTimerNode(InTimerId);

			if (TimerNodePtr == nullptr)
			{
				// List of timers in TimersView not up to date?
				// Refresh and try again.
				TimersView->RebuildTree(false);
				TimerNodePtr = TimersView->GetTimerNode(InTimerId);
			}

			return TimerNodePtr;
		}
	}
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::SetSelectedTimer(uint32 InTimerId)
{
	if (InTimerId != SelectedTimerId)
	{
		SelectedTimerId = InTimerId;

		if (SelectedTimerId != InvalidTimerId)
		{
			UpdateCallersAndCallees();

			TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
			if (Wnd)
			{
				TSharedPtr<STimersView> TimersView = Wnd->GetTimersView();
				if (TimersView)
				{
					TimersView->SelectTimerNode(InTimerId);
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::OnThreadFilterChanged()
{
	UpdateCallersAndCallees();
	UpdateAggregatedTimerStats();
	UpdateAggregatedCounterStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ResetCallersAndCallees()
{
	TimerButterflyAggregator->Cancel();
	TimerButterflyAggregator->SetTimeInterval(0.0, 0.0);

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd)
	{
		TSharedPtr<STimerTreeView> CallersTreeView = Wnd->GetCallersTreeView();
		TSharedPtr<STimerTreeView> CalleesTreeView = Wnd->GetCalleesTreeView();

		if (CallersTreeView)
		{
			CallersTreeView->Reset();
		}

		if (CalleesTreeView)
		{
			CalleesTreeView->Reset();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::UpdateCallersAndCallees()
{
	if (SelectionStartTime < SelectionEndTime && SelectedTimerId != InvalidTimerId)
	{
		TimerButterflyAggregator->Cancel();
		TimerButterflyAggregator->SetTimeInterval(SelectionStartTime, SelectionEndTime);

		TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
		if (Wnd)
		{
			TSharedPtr<STimerTreeView> CallersTreeView = Wnd->GetCallersTreeView();
			TSharedPtr<STimerTreeView> CalleesTreeView = Wnd->GetCalleesTreeView();

			if (CallersTreeView)
			{
				CallersTreeView->Reset();
			}

			if (CalleesTreeView)
			{
				CalleesTreeView->Reset();
			}

			if (CallersTreeView || CalleesTreeView)
			{
				TimerButterflyAggregator->Start();
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::UpdateAggregatedTimerStats()
{
	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd)
	{
		TSharedPtr<STimersView> TimersView = Wnd->GetTimersView();
		if (TimersView)
		{
			TimersView->UpdateStats(SelectionStartTime, SelectionEndTime);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::UpdateAggregatedCounterStats()
{
	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd)
	{
		TSharedPtr<SStatsView> StatsView = Wnd->GetStatsView();
		if (StatsView)
		{
			StatsView->UpdateStats(SelectionStartTime, SelectionEndTime);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
