// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LoadingProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/Table/Widgets/STableTreeView.h"
#include "Insights/InsightsManager.h"
#include "Insights/LoadingProfiler/Widgets/SLoadingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "LoadingProfilerManager"

//DEFINE_LOG_CATEGORY(LoadingProfiler);
//
//DEFINE_STAT(STAT_FT_OnPaint);
//DEFINE_STAT(STAT_GT_OnPaint);
//DEFINE_STAT(STAT_TT_OnPaint);
//DEFINE_STAT(STAT_IOPM_Tick);

TSharedPtr<FLoadingProfilerManager> FLoadingProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

FLoadingProfilerManager::FLoadingProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: CommandList(InCommandList)
	, ActionManager(this)
	, ProfilerWindow(nullptr)
	, bIsTimingViewVisible(true)
	, bIsEventAggregationTreeViewVisible(true)
	, bIsObjectTypeAggregationTreeViewVisible(true)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::PostConstructor()
{
	// Register tick functions.
	//OnTick = FTickerDelegate::CreateSP(this, &FLoadingProfilerManager::Tick);
	//OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

	FLoadingProfilerCommands::Register();
	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::BindCommands()
{
	ActionManager.Map_ToggleTimingViewVisibility_Global();
	ActionManager.Map_ToggleEventAggregationTreeViewVisibility_Global();
	ActionManager.Map_ToggleObjectTypeAggregationTreeViewVisibility_Global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLoadingProfilerManager::~FLoadingProfilerManager()
{
	FLoadingProfilerCommands::Unregister();

	// Unregister tick function.
	//FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FLoadingProfilerManager> FLoadingProfilerManager::Get()
{
	return FLoadingProfilerManager::Instance;
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
	//SCOPE_CYCLE_COUNTER(STAT_IOPM_Tick);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::OnSessionChanged()
{
	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::ShowHideTimingView(const bool bTimingViewVisibleState)
{
	//bool bWasTimingViewVisible = bIsTimingViewVisible;

	bIsTimingViewVisible = bTimingViewVisibleState;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FLoadingProfilerTabs::TimingViewID, bIsTimingViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::ShowHideEventAggregationTreeView(const bool bEventAggregationTreeViewVisibleState)
{
	//bool bWasEventAggregationTreeViewVisible = bIsEventAggregationTreeViewVisible;

	bIsEventAggregationTreeViewVisible = bEventAggregationTreeViewVisibleState;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FLoadingProfilerTabs::EventAggregationTreeViewID, bIsEventAggregationTreeViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingProfilerManager::ShowHideObjectTypeAggregationTreeView(const bool bObjectTypeAggregationTreeViewVisibleState)
{
	//bool bWasObjectTypeAggregationTreeViewVisible = bIsObjectTypeAggregationTreeViewVisible;

	bIsObjectTypeAggregationTreeViewVisible = bObjectTypeAggregationTreeViewVisibleState;

	TSharedPtr<SLoadingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FLoadingProfilerTabs::ObjectTypeAggregationTreeViewID, bIsObjectTypeAggregationTreeViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
