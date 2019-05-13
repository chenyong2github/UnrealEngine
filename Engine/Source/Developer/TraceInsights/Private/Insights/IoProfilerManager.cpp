// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IoProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "Templates/ScopedPointer.h"
#include "Templates/UniquePtr.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/InsightsManager.h"
//#include "Insights/IoProfilerCommon.h"
#include "Insights/Widgets/SFrameTrack.h"
#include "Insights/Widgets/SGraphTrack.h"
#include "Insights/Widgets/SLogView.h"
#include "Insights/Widgets/STimersView.h"
#include "Insights/Widgets/STimingView.h"
#include "Insights/Widgets/SIoProfilerWindow.h"


////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "IoProfilerManager"

//DEFINE_LOG_CATEGORY(IoProfiler);
//
//DEFINE_STAT(STAT_FT_OnPaint);
//DEFINE_STAT(STAT_GT_OnPaint);
//DEFINE_STAT(STAT_TT_OnPaint);
//DEFINE_STAT(STAT_IOPM_Tick);

TSharedPtr<FIoProfilerManager> FIoProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

FIoProfilerManager::FIoProfilerManager(TSharedRef<FUICommandList> InCommandList)
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

void FIoProfilerManager::PostConstructor()
{
	// Register tick functions.
	//OnTick = FTickerDelegate::CreateSP(this, &FIoProfilerManager::Tick);
	//OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

	FIoProfilerCommands::Register();
	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FIoProfilerManager::BindCommands()
{
	ActionManager.Map_ToggleFramesTrackVisibility_Global();
	ActionManager.Map_ToggleGraphTrackVisibility_Global();
	ActionManager.Map_ToggleTimingTrackVisibility_Global();
	ActionManager.Map_ToggleTimersViewVisibility_Global();
	ActionManager.Map_ToggleLogViewVisibility_Global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FIoProfilerManager::~FIoProfilerManager()
{
	FIoProfilerCommands::Unregister();

	// Unregister tick function.
	//FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FIoProfilerManager> FIoProfilerManager::Get()
{
	return FIoProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedRef<FUICommandList> FIoProfilerManager::GetCommandList() const
{
	return CommandList;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FIoProfilerCommands& FIoProfilerManager::GetCommands()
{
	return FIoProfilerCommands::Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FIoProfilerActionManager& FIoProfilerManager::GetActionManager()
{
	return FIoProfilerManager::Instance->ActionManager;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FIoProfilerManager::Tick(float DeltaTime)
{
	//SCOPE_CYCLE_COUNTER(STAT_IOPM_Tick);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FIoProfilerManager::OnSessionChanged()
{
	TSharedPtr<SIoProfilerWindow> Wnd = GetProfilerWindow();
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

void FIoProfilerManager::SetTimersViewVisible(const bool bTimersViewVisibleState)
{
	bool bWasTimersViewVisible = bIsTimersViewVisible;

	bIsTimersViewVisible = bTimersViewVisibleState;

	if (bIsTimersViewVisible && !bWasTimersViewVisible && FInsightsManager::Get()->GetSession().IsValid())
	{
		TSharedPtr<SIoProfilerWindow> Wnd = GetProfilerWindow();
		if (Wnd.IsValid())
		{
			Wnd->TimersView->RebuildTree();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
