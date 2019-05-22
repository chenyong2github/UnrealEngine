// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InsightsManager.h"

#include "Modules/ModuleManager.h"
#include "Templates/ScopedPointer.h"
#include "Templates/UniquePtr.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "InsightsManager"

TSharedPtr<FInsightsManager> FInsightsManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsManager::FInsightsManager(TSharedRef<Trace::IAnalysisService> InTraceAnalysisService, TSharedRef<Trace::ISessionService> InSessionService)
	: AnalysisService(InTraceAnalysisService)
	, SessionService(InSessionService)
	, CommandList(new FUICommandList())
	, ActionManager(this)
	, Settings()
	, bIsDebugInfoEnabled(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::PostConstructor()
{
	// Register tick functions.
	//OnTick = FTickerDelegate::CreateSP(this, &FInsightsManager::Tick);
	//OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

	FInsightsCommands::Register();
	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::BindCommands()
{
	ActionManager.Map_InsightsManager_Live();
	ActionManager.Map_InsightsManager_Load();
	ActionManager.Map_InsightsManager_Mock();
	ActionManager.Map_ToggleDebugInfo_Global();
	ActionManager.Map_OpenSettings_Global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsManager::~FInsightsManager()
{
	FInsightsCommands::Unregister();

	// Unregister tick function.
	//FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FInsightsManager> FInsightsManager::Get()
{
	return FInsightsManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<const Trace::IAnalysisSession> FInsightsManager::GetSession() const
{
	return Session;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedRef<FUICommandList> FInsightsManager::GetCommandList() const
{
	return CommandList;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FInsightsCommands& FInsightsManager::GetCommands()
{
	return FInsightsCommands::Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsActionManager& FInsightsManager::GetActionManager()
{
	return FInsightsManager::Instance->ActionManager;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsSettings& FInsightsManager::GetSettings()
{
	return FInsightsManager::Instance->Settings;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FInsightsManager::Tick(float DeltaTime)
{
	//SCOPE_CYCLE_COUNTER(STAT_IM_Tick);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::ResetSession()
{
	Session = nullptr;

	FTimingProfilerManager::Get()->OnSessionChanged();
	SessionChangedEvent.Broadcast();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::CreateMockSession()
{
	ResetSession();

	Session = AnalysisService->MockAnalysis();

	FTimingProfilerManager::Get()->OnSessionChanged();
	SessionChangedEvent.Broadcast();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::CreateLiveSession()
{
	ResetSession();

	Session = nullptr;
	TArray<Trace::FSessionHandle> AvailableSessions;
	SessionService->GetAvailableSessions(AvailableSessions);
	for (Trace::FSessionHandle SessionHandle : AvailableSessions)
	{
		Trace::FSessionInfo SessionInfo;
		SessionService->GetSessionInfo(SessionHandle, SessionInfo);
		if (SessionInfo.IsLive)
		{
			TUniquePtr<Trace::IInDataStream> DataStream(SessionService->OpenSessionStream(SessionHandle));
			check(DataStream);
			Session = AnalysisService->StartAnalysis(TEXT("Live session"), MoveTemp(DataStream));
			break;
		}
	}
	if (!Session && AvailableSessions.Num())
	{
		Trace::FSessionHandle SessionHandle = AvailableSessions.Last();
		TUniquePtr<Trace::IInDataStream> DataStream(SessionService->OpenSessionStream(SessionHandle));
		check(DataStream);
		Session = AnalysisService->StartAnalysis(TEXT("Latest session"), MoveTemp(DataStream));
	}

	FTimingProfilerManager::Get()->OnSessionChanged();
	SessionChangedEvent.Broadcast();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::LoadTraceFile(const FString& TraceFilepath)
{
	ResetSession();

	TUniquePtr<Trace::IInDataStream> DataStream(SessionService->OpenSessionFromFile(*TraceFilepath));
	check(DataStream);

	Session = AnalysisService->StartAnalysis(*TraceFilepath, MoveTemp(DataStream));

	FTimingProfilerManager::Get()->OnSessionChanged();
	SessionChangedEvent.Broadcast();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::OpenSettings()
{
	FTimingProfilerManager::Get()->GetProfilerWindow()->OpenProfilerSettings();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
