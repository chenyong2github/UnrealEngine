// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimingProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/Widgets/SFrameTrack.h"
#include "Insights/Widgets/SGraphTrack.h"
#include "Insights/Widgets/SLogView.h"
#include "Insights/Widgets/SStatsView.h"
#include "Insights/Widgets/STimersView.h"
#include "Insights/Widgets/STimingView.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "TimingProfilerManager"

DEFINE_LOG_CATEGORY(TimingProfiler);

DEFINE_STAT(STAT_FT_OnPaint);
DEFINE_STAT(STAT_GT_OnPaint);
DEFINE_STAT(STAT_TT_OnPaint);
DEFINE_STAT(STAT_TPM_Tick);

TSharedPtr<FTimingProfilerManager> FTimingProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingProfilerManager::FTimingProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: CommandList(InCommandList)
	, ActionManager(this)
	, ProfilerWindow(nullptr)
	, bIsFramesTrackVisible(true)
	, bIsGraphTrackVisible(false)
	, bIsTimingViewVisible(true)
	, bIsTimersViewVisible(true)
	, bIsStatsCountersViewVisible(true)
	, bIsLogViewVisible(true)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::PostConstructor()
{
	// Register tick functions.
	//OnTick = FTickerDelegate::CreateSP(this, &FTimingProfilerManager::Tick);
	//OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

	FTimingProfilerCommands::Register();
	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::BindCommands()
{
	ActionManager.Map_ToggleFramesTrackVisibility_Global();
	ActionManager.Map_ToggleGraphTrackVisibility_Global();
	ActionManager.Map_ToggleTimingViewVisibility_Global();
	ActionManager.Map_ToggleTimersViewVisibility_Global();
	ActionManager.Map_ToggleStatsCountersViewVisibility_Global();
	ActionManager.Map_ToggleLogViewVisibility_Global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingProfilerManager::~FTimingProfilerManager()
{
	FTimingProfilerCommands::Unregister();

	// Unregister tick function.
	//FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTimingProfilerManager> FTimingProfilerManager::Get()
{
	return FTimingProfilerManager::Instance;
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
	SCOPE_CYCLE_COUNTER(STAT_TPM_Tick);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::OnSessionChanged()
{
	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideFramesTrack(const bool bFramesTrackVisibleState)
{
	bIsFramesTrackVisible = bFramesTrackVisibleState;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::FramesTrackID, bIsFramesTrackVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideGraphTrack(const bool bGraphTrackVisibleState)
{
	bIsGraphTrackVisible = bGraphTrackVisibleState;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::GraphTrackID, bIsGraphTrackVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideTimingView(const bool bTimingViewVisibleState)
{
	bIsTimingViewVisible = bTimingViewVisibleState;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::TimingViewID, bIsTimingViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideTimersView(const bool bTimersViewVisibleState)
{
	bIsTimersViewVisible = bTimersViewVisibleState;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::TimersID, bIsTimersViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideStatsCountersView(const bool bStatsCountersViewVisibleState)
{
	bIsStatsCountersViewVisible = bStatsCountersViewVisibleState;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::StatsCountersID, bIsStatsCountersViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::ShowHideLogView(const bool bLogViewVisibleState)
{
	bIsLogViewVisible = bLogViewVisibleState;

	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FTimingProfilerTabs::LogViewID, bIsLogViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FTimerNodePtr FTimingProfilerManager::GetTimerNode(uint64 TypeId) const
{
	const FTimerNodePtr* TimerNodePtrPtr = nullptr;
	TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		TSharedPtr<STimersView> TimersView = Wnd->GetTimersView();
		if (TimersView.IsValid())
		{
			TimerNodePtrPtr = TimersView->GetTimerNode(TypeId);
			if (TimerNodePtrPtr == nullptr)
			{
				// List of timers in TimersView not up to date?
				// Refresh and try again.
				TimersView->RebuildTree(false);
				TimerNodePtrPtr = TimersView->GetTimerNode(TypeId);
			}
		}
	}
	return TimerNodePtrPtr ? *TimerNodePtrPtr : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
