// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkingProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "NetworkingProfilerManager"

DEFINE_LOG_CATEGORY(NetworkingProfiler);

TSharedPtr<FNetworkingProfilerManager> FNetworkingProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FNetworkingProfilerManager> FNetworkingProfilerManager::Get()
{
	return FNetworkingProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkingProfilerManager::FNetworkingProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: CommandList(InCommandList)
	, ActionManager(this)
	, ProfilerWindows()
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

FNetworkingProfilerManager::~FNetworkingProfilerManager()
{
	FNetworkingProfilerCommands::Unregister();

	// Unregister tick function.
	//FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::BindCommands()
{
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
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::OnSessionChanged()
{
	for (TWeakPtr<SNetworkingProfilerWindow> WndWeakPtr : ProfilerWindows)
	{
		TSharedPtr<SNetworkingProfilerWindow> Wnd = WndWeakPtr.Pin();
		if (Wnd.IsValid())
		{
			Wnd->Reset();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
