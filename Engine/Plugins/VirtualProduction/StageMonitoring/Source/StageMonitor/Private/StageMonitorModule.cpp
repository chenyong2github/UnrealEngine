// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageMonitorModule.h"

#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "StageMonitor.h"
#include "StageMonitoringSettings.h"
#include "StageMonitorSessionManager.h"

const FName IStageMonitorModule::ModuleName = TEXT("StageMonitor");


DEFINE_LOG_CATEGORY(LogStageMonitor)


void FStageMonitorModule::StartupModule()
{
	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FStageMonitorModule::OnEngineLoopInitComplete);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	CommandStart = MakeUnique<FAutoConsoleCommand>(TEXT("StageMonitor.Monitor.Start")
													, TEXT("Start Stage monitoring")
													, FConsoleCommandDelegate::CreateRaw(this, &FStageMonitorModule::StartMonitor));

	CommandStop = MakeUnique<FAutoConsoleCommand>(TEXT("StageMonitor.Monitor.Stop")
												, TEXT("Stop Stage monitoring")
												, FConsoleCommandDelegate::CreateRaw(this, &FStageMonitorModule::StopMonitor));
#endif 
}

void FStageMonitorModule::ShutdownModule()
{
	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);
	StageMonitor.Reset();
}

void FStageMonitorModule::OnEngineLoopInitComplete()
{
	SessionManager = MakeUnique<FStageMonitorSessionManager>();
	StageMonitor = MakeUnique<FStageMonitor>();
	StageMonitor->Initialize();
	const UStageMonitoringSettings* Settings = GetDefault<UStageMonitoringSettings>();
	if (Settings->MonitorSettings.ShouldAutoStartOnLaunch())
	{
		StageMonitor->Start();
	}
}

void FStageMonitorModule::StartMonitor()
{
	StageMonitor->Start();
}

void FStageMonitorModule::StopMonitor()
{
	StageMonitor->Stop();
}

IStageMonitor& FStageMonitorModule::GetStageMonitor()
{
	return *StageMonitor;
}

IStageMonitorSessionManager& FStageMonitorModule::GetStageMonitorSessionManager()
{
	return *SessionManager;
}

IMPLEMENT_MODULE(FStageMonitorModule, StageMonitor)
