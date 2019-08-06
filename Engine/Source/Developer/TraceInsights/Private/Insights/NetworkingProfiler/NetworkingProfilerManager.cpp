// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetworkingProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/NetworkingProfiler/Widgets/SDataStreamBreakdownView.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerWindow.h"
#include "Insights/NetworkingProfiler/Widgets/SPacketBreakdownView.h"
#include "Insights/NetworkingProfiler/Widgets/SPacketSizesView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "NetworkingProfilerManager"

//DEFINE_LOG_CATEGORY(NetworkingProfiler);
//
//DEFINE_STAT(STAT_FT_OnPaint);
//DEFINE_STAT(STAT_GT_OnPaint);
//DEFINE_STAT(STAT_TT_OnPaint);
//DEFINE_STAT(STAT_IOPM_Tick);

TSharedPtr<FNetworkingProfilerManager> FNetworkingProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkingProfilerManager::FNetworkingProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: CommandList(InCommandList)
	, ActionManager(this)
	, ProfilerWindow(nullptr)
	, bIsPacketSizesViewVisible(true)
	, bIsPacketBreakdownViewVisible(true)
	, bIsDataStreamBreakdownViewVisible(true)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::PostConstructor()
{
	// Register tick functions.
	//OnTick = FTickerDelegate::CreateSP(this, &FNetworkingProfilerManager::Tick);
	//OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

	FNetworkingProfilerCommands::Register();
	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::BindCommands()
{
	ActionManager.Map_TogglePacketSizesViewVisibility_Global();
	ActionManager.Map_TogglePacketBreakdownViewVisibility_Global();
	ActionManager.Map_ToggleDataStreamBreakdownViewVisibility_Global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkingProfilerManager::~FNetworkingProfilerManager()
{
	FNetworkingProfilerCommands::Unregister();

	// Unregister tick function.
	//FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FNetworkingProfilerManager> FNetworkingProfilerManager::Get()
{
	return FNetworkingProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedRef<FUICommandList> FNetworkingProfilerManager::GetCommandList() const
{
	return CommandList;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FNetworkingProfilerCommands& FNetworkingProfilerManager::GetCommands()
{
	return FNetworkingProfilerCommands::Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkingProfilerActionManager& FNetworkingProfilerManager::GetActionManager()
{
	return FNetworkingProfilerManager::Instance->ActionManager;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FNetworkingProfilerManager::Tick(float DeltaTime)
{
	//SCOPE_CYCLE_COUNTER(STAT_IOPM_Tick);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::OnSessionChanged()
{
	TSharedPtr<SNetworkingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::ShowHidePacketSizesView(const bool bPacketSizesViewVisibleState)
{
	//bool bWasPacketSizesViewVisible = bIsPacketSizesViewVisible;

	bIsPacketSizesViewVisible = bPacketSizesViewVisibleState;

	TSharedPtr<SNetworkingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FNetworkingProfilerTabs::PacketSizesViewID, bIsPacketSizesViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::ShowHidePacketBreakdownView(const bool bPacketBreakdownViewVisibleState)
{
	//bool bWasPacketBreakdownViewVisible = bIsPacketBreakdownViewVisible;

	bIsPacketBreakdownViewVisible = bPacketBreakdownViewVisibleState;

	TSharedPtr<SNetworkingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FNetworkingProfilerTabs::PacketBreakdownViewID, bIsPacketBreakdownViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::ShowHideDataStreamBreakdownView(const bool bDataStreamBreakdownViewVisibleState)
{
	//bool bWasDataStreamBreakdownViewVisible = bIsDataStreamBreakdownViewVisible;

	bIsDataStreamBreakdownViewVisible = bDataStreamBreakdownViewVisibleState;

	TSharedPtr<SNetworkingProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FNetworkingProfilerTabs::DataStreamBreakdownViewID, bIsDataStreamBreakdownViewVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
