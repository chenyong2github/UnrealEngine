// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InsightsManager.h"

#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"
#include "Templates/ScopedPointer.h"
#include "Templates/UniquePtr.h"

// Insights
#include "Insights/IoProfilerManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/SStartPageWindow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "InsightsManager"

const FName FInsightsManagerTabs::StartPageTabId(TEXT("StartPage"));
const FName FInsightsManagerTabs::TimingProfilerTabId(TEXT("TimingProfiler"));
const FName FInsightsManagerTabs::IoProfilerTabId(TEXT("IoProfiler"));

TSharedPtr<FInsightsManager> FInsightsManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsManager::FInsightsManager(TSharedRef<Trace::IAnalysisService> InTraceAnalysisService,
								   TSharedRef<Trace::ISessionService> InTraceSessionService,
								   TSharedRef<Trace::IModuleService> InTraceModuleService)
	: AnalysisService(InTraceAnalysisService)
	, SessionService(InTraceSessionService)
	, ModuleService(InTraceModuleService)
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
	ActionManager.Map_InsightsManager_Load();
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
	if (Session.IsValid())
	{
		Session.Reset();
		OnSessionChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::OnSessionChanged()
{
	if (TSharedPtr<FTimingProfilerManager> TimingProfilerManager = FTimingProfilerManager::Get())
	{
		// FIXME: make TimingProfilerManager to register to SessionChangedEvent instead
		TimingProfilerManager->OnSessionChanged();
	}

	if (TSharedPtr<FIoProfilerManager> IoProfilerManager = FIoProfilerManager::Get())
	{
		// FIXME: make IoProfilerManager to register to SessionChangedEvent instead
		IoProfilerManager->OnSessionChanged();
	}

	SessionChangedEvent.Broadcast();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::SpawnAndActivateTabs()
{
	// Open tabs for profilers.
	if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::TimingProfilerTabId))
	{
		FGlobalTabmanager::Get()->InvokeTab(FInsightsManagerTabs::TimingProfilerTabId);
	}
	if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::IoProfilerTabId))
	{
		FGlobalTabmanager::Get()->InvokeTab(FInsightsManagerTabs::IoProfilerTabId);
	}

	// Ensure Timing Insights / Timing View is the active tab / view.
	if (TSharedPtr<SDockTab> TimingInsightsTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FInsightsManagerTabs::TimingProfilerTabId))
	{
		TimingInsightsTab->ActivateInParent(ETabActivationCause::SetDirectly);

		//TOOD: FTimingProfilerManager::Get()->SpawnAndActivateTabs();
		TSharedPtr<class STimingProfilerWindow> Wnd = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Wnd)
		{
			TSharedPtr<FTabManager> TabManager = Wnd->GetTabManager();

			if (TSharedPtr<SDockTab> TimingViewTab = TabManager->FindExistingLiveTab(FTimingProfilerTabs::TimingViewID))
			{
				TimingViewTab->ActivateInParent(ETabActivationCause::SetDirectly);
				FSlateApplication::Get().SetKeyboardFocus(TimingViewTab->GetContent());
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FInsightsManager::IsAnyLiveSessionAvailable(Trace::FSessionHandle& OutLastLiveSessionHandle) const
{
	TArray<Trace::FSessionHandle> LiveSessions;
	SessionService->GetLiveSessions(LiveSessions);

	if (LiveSessions.Num() > 0)
	{
		OutLastLiveSessionHandle = LiveSessions.Last();
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FInsightsManager::IsAnySessionAvailable(Trace::FSessionHandle& OutLastSessionHandle) const
{
	TArray<Trace::FSessionHandle> AvailableSessions;
	SessionService->GetAvailableSessions(AvailableSessions);

	if (AvailableSessions.Num() > 0)
	{
		OutLastSessionHandle = AvailableSessions.Last();
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::LoadLastLiveSession()
{
	ResetSession();

	TArray<Trace::FSessionHandle> AvailableSessions;
	SessionService->GetAvailableSessions(AvailableSessions);

	// Iterate in reverse order as we want the most recent live session first.
	for (int32 SessionIndex = AvailableSessions.Num() - 1; SessionIndex >= 0; --SessionIndex)
	{
		Trace::FSessionHandle SessionHandle = AvailableSessions[SessionIndex];

		Trace::FSessionInfo SessionInfo;
		SessionService->GetSessionInfo(SessionHandle, SessionInfo);

		if (SessionInfo.bIsLive)
		{
			LoadSession(SessionHandle);
			break;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::LoadLastSession()
{
	ResetSession();

	TArray<Trace::FSessionHandle> AvailableSessions;
	SessionService->GetAvailableSessions(AvailableSessions);

	if (AvailableSessions.Num() > 0)
	{
		LoadSession(AvailableSessions.Last());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::LoadSession(Trace::FSessionHandle SessionHandle)
{
	ResetSession();

	Trace::FSessionInfo SessionInfo;
	SessionService->GetSessionInfo(SessionHandle, SessionInfo);

	TUniquePtr<Trace::IInDataStream> DataStream(SessionService->OpenSessionStream(SessionHandle));
	if (DataStream)
	{
		Session = AnalysisService->StartAnalysis(SessionInfo.Name, MoveTemp(DataStream));
		SpawnAndActivateTabs();
		OnSessionChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::LoadTraceFile(const FString& TraceFilepath)
{
	ResetSession();

	TUniquePtr<Trace::IInDataStream> DataStream(SessionService->OpenSessionFromFile(*TraceFilepath));
	if (DataStream)
	{
		Session = AnalysisService->StartAnalysis(*TraceFilepath, MoveTemp(DataStream));
		SpawnAndActivateTabs();
		OnSessionChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::OpenSettings()
{
	TSharedPtr<SStartPageWindow> Wnd = GetStartPageWindow();
	if (Wnd.IsValid())
	{
		Wnd->OpenSettings();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
