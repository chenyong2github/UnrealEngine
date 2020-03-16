// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionInsightsManager.h"

const FName FNetworkPredictionInsightsTabs::ToolbarID("Toolbar");
const FName FNetworkPredictionInsightsTabs::SimFrameViewID("SimFrameView");
const FName FNetworkPredictionInsightsTabs::SimFrameContentsID("SimFrameContents");


TSharedPtr<FNetworkPredictionInsightsManager> FNetworkPredictionInsightsManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkPredictionInsightsManager::FNetworkPredictionInsightsManager()
	//: CommandList(MakeShared<FUICommandList>())
	: ActionManager(this)
{
	//FInsightsManager::Get()->GetCommandList()
}

void FNetworkPredictionInsightsManager::PostConstructor()
{
	// Register tick functions.
	//OnTick = FTickerDelegate::CreateSP(this, &FNetworkPredictionManager::Tick);
	//OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

	FNetworkPredictionInsightsCommands::Register();
	BindCommands();
}

void FNetworkPredictionInsightsManager::BindCommands()
{
	//ActionManager.Map_
	//CommandList->MapAction(GetCommands().NextEngineFrame, FUIAction::FExecuteAction
}

FNetworkPredictionInsightsManager::~FNetworkPredictionInsightsManager()
{
	FNetworkPredictionInsightsCommands::Unregister();

	// Unregister tick function.
	//FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);
}

TSharedPtr<FNetworkPredictionInsightsManager> FNetworkPredictionInsightsManager::Get()
{
	return FNetworkPredictionInsightsManager::Instance;
}
/*
const TSharedRef<FUICommandList> FNetworkPredictionInsightsManager::GetCommandList() const
{
	return CommandList;
}
*/

const FNetworkPredictionInsightsCommands& FNetworkPredictionInsightsManager::GetCommands()
{
	return FNetworkPredictionInsightsCommands::Get();
}

FNetworkPredictionInsightsActionManager& FNetworkPredictionInsightsManager::GetActionManager()
{
	return FNetworkPredictionInsightsManager::Instance->ActionManager;
}

bool FNetworkPredictionInsightsManager::Tick(float DeltaTime)
{
	//SCOPE_CYCLE_COUNTER(STAT_IOPM_Tick);
	return true;
}

void FNetworkPredictionInsightsManager::OnSessionChanged()
{
	for (TWeakPtr<SNPWindow> WndWeakPtr : NetworkPredictionInsightsWindows)
	{
		TSharedPtr<SNPWindow> Wnd = WndWeakPtr.Pin();
		if (Wnd.IsValid())
		{
			Wnd->Reset();
		}
	}
}