// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimingProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "Templates/ScopedPointer.h"
#include "Templates/UniquePtr.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/Widgets/SFrameTrack.h"
#include "Insights/Widgets/SGraphTrack.h"
#include "Insights/Widgets/SLogView.h"
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
	, bIsTimingTrackVisible(true)
	, bIsTimersViewVisible(true)
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
	ActionManager.Map_ToggleTimingTrackVisibility_Global();
	ActionManager.Map_ToggleTimersViewVisibility_Global();
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
		Wnd->FrameTrack->Reset();
		Wnd->GraphTrack->Reset();
		Wnd->TimingView->Reset();
		Wnd->TimersView->RebuildTree();
		Wnd->LogView->Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerManager::SetTimersViewVisible(const bool bTimersViewVisibleState)
{
	bool bWasTimersViewVisible = bIsTimersViewVisible;

	bIsTimersViewVisible = bTimersViewVisibleState;

	if (bIsTimersViewVisible && !bWasTimersViewVisible && FInsightsManager::Get()->GetSession().IsValid())
	{
		TSharedPtr<STimingProfilerWindow> Wnd = GetProfilerWindow();
		if (Wnd.IsValid())
		{
			Wnd->TimersView->RebuildTree();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
