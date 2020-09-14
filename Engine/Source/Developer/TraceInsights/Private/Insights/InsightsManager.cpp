// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsManager.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/CString.h"
#include "Modules/ModuleManager.h"
#include "Templates/UniquePtr.h"
#include "Trace/StoreClient.h"
#include "TraceServices/Model/NetProfiler.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

// Insights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsStyle.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/LoadingProfiler/LoadingProfilerManager.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerManager.h"
#include "Insights/Tests/InsightsTestRunner.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/InsightsMessageLogViewModel.h"
#include "Insights/Widgets/SStartPageWindow.h"
#include "Insights/Widgets/SSessionInfoWindow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "InsightsManager"

const FName FInsightsManagerTabs::StartPageTabId(TEXT("StartPage"));
const FName FInsightsManagerTabs::SessionInfoTabId(TEXT("SessionInfo"));
const FName FInsightsManagerTabs::TimingProfilerTabId(TEXT("TimingProfiler"));
const FName FInsightsManagerTabs::LoadingProfilerTabId(TEXT("LoadingProfiler"));
const FName FInsightsManagerTabs::NetworkingProfilerTabId(TEXT("NetworkingProfiler"));
const FName FInsightsManagerTabs::MemoryProfilerTabId(TEXT("MemoryProfiler"));
const FName FInsightsManagerTabs::InsightsMessageLogTabId(TEXT("MessageLog"));
const FName FInsightsManagerTabs::AutomationWindowTabId(TEXT("AutomationWindow"));

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAvailabilityCheck
////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAvailabilityCheck::Tick()
{
	if (NextTimestamp != (uint64)-1)
	{
		const uint64 Time = FPlatformTime::Cycles64();
		if (Time > NextTimestamp)
		{
			// Increase wait time with 0.1s, but at no more than 3s.
			WaitTime = FMath::Min(WaitTime + 0.1, 3.0);
			const uint64 WaitTimeCycles64 = static_cast<uint64>(WaitTime / FPlatformTime::GetSecondsPerCycle64());
			NextTimestamp = Time + WaitTimeCycles64;

			return true; // yes, manager can check for (slow) availability conditions
		}
	}

	return false; // no, manager should not check for (slow) availability conditions during this tick
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAvailabilityCheck::Disable()
{
	WaitTime = 0.0;
	NextTimestamp = (uint64)-1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAvailabilityCheck::Enable(double InWaitTime)
{
	WaitTime = InWaitTime;
	const uint64 WaitTimeCycles64 = static_cast<uint64>(WaitTime / FPlatformTime::GetSecondsPerCycle64());
	NextTimestamp = FPlatformTime::Cycles64() + WaitTimeCycles64;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FInsightsManager
////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FInsightsManager::AutoQuitMsgOnFail = TEXT("Application is closing because it was started with the AutoQuit parameter and session analysis failed to start.");

TSharedPtr<FInsightsManager> FInsightsManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FInsightsManager> FInsightsManager::Get()
{
	return FInsightsManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FInsightsManager> FInsightsManager::CreateInstance(TSharedRef<Trace::IAnalysisService> TraceAnalysisService,
															  TSharedRef<Trace::IModuleService> TraceModuleService)
{
	ensure(!FInsightsManager::Instance.IsValid());
	if (FInsightsManager::Instance.IsValid())
	{
		FInsightsManager::Instance.Reset();
	}

	FInsightsManager::Instance = MakeShared<FInsightsManager>(TraceAnalysisService, TraceModuleService);

	return FInsightsManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsManager::FInsightsManager(TSharedRef<Trace::IAnalysisService> InTraceAnalysisService,
								   TSharedRef<Trace::IModuleService> InTraceModuleService)
	: bIsInitialized(false)
	, AnalysisService(InTraceAnalysisService)
	, ModuleService(InTraceModuleService)
	, StoreDir()
	, StoreClient()
	, CommandList(new FUICommandList())
	, ActionManager(this)
	, Settings()
	, bIsDebugInfoEnabled(false)
#if WITH_EDITOR
	, bShouldOpenAnalysisInSeparateProcess(false)
#else
	, bShouldOpenAnalysisInSeparateProcess(true)
#endif
	, AnalysisStopwatch()
	, bIsAnalysisComplete(false)
	, SessionDuration(0.0)
	, AnalysisDuration(0.0)
	, AnalysisSpeedFactor(0.0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::Initialize(IUnrealInsightsModule& InsightsModule)
{
	ensure(!bIsInitialized);
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;

	InsightsMessageLogViewModel = MakeShared<FInsightsMessageLogViewModel>("InsightsLog", InsightsMessageLog);

	InsightsMenuBuilder = MakeShared<FInsightsMenuBuilder>();

	// Register tick functions.
	OnTick = FTickerDelegate::CreateSP(this, &FInsightsManager::Tick);
	OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 0.0f);

	FInsightsCommands::Register();
	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}
	bIsInitialized = false;

	ResetSession(false);

	FInsightsCommands::Unregister();

	// Unregister tick function.
	FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);

	FInsightsManager::Instance.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsManager::~FInsightsManager()
{
	ensure(!bIsInitialized);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::BindCommands()
{
	ActionManager.Map_InsightsManager_Load();
	ActionManager.Map_ToggleDebugInfo_Global();
	ActionManager.Map_OpenSettings_Global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::RegisterMajorTabs(IUnrealInsightsModule& InsightsModule)
{
	const FInsightsMajorTabConfig& StartPageConfig = InsightsModule.FindMajorTabConfig(FInsightsManagerTabs::StartPageTabId);
	if (StartPageConfig.bIsAvailable)
	{
		// Register tab spawner for the Start Page.
		FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::StartPageTabId,
			FOnSpawnTab::CreateRaw(this, &FInsightsManager::SpawnStartPageTab))
			.SetDisplayName(StartPageConfig.TabLabel.IsSet() ? StartPageConfig.TabLabel.GetValue() : LOCTEXT("StartPageTabTitle", "Unreal Insights"))
			.SetTooltipText(StartPageConfig.TabTooltip.IsSet() ? StartPageConfig.TabTooltip.GetValue() : LOCTEXT("StartPageTooltipText", "Open the start page for Unreal Insights."))
			.SetIcon(StartPageConfig.TabIcon.IsSet() ? StartPageConfig.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "StartPage.Icon.Small"));

		TSharedRef<FWorkspaceItem> Group = StartPageConfig.WorkspaceGroup.IsValid() ? StartPageConfig.WorkspaceGroup.ToSharedRef() : WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory();
		TabSpawnerEntry.SetGroup(Group);
	}

	const FInsightsMajorTabConfig& SessionInfoConfig = InsightsModule.FindMajorTabConfig(FInsightsManagerTabs::SessionInfoTabId);
	if (SessionInfoConfig.bIsAvailable)
	{
		// Register tab spawner for the Session Info.
		FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::SessionInfoTabId,
			FOnSpawnTab::CreateRaw(this, &FInsightsManager::SpawnSessionInfoTab))
			.SetDisplayName(SessionInfoConfig.TabLabel.IsSet() ? SessionInfoConfig.TabLabel.GetValue() : LOCTEXT("SessionInfoTabTitle", "Session"))
			.SetTooltipText(SessionInfoConfig.TabTooltip.IsSet() ? SessionInfoConfig.TabTooltip.GetValue() : LOCTEXT("SessionInfoTooltipText", "Open the Session tab."))
			.SetIcon(SessionInfoConfig.TabIcon.IsSet() ? SessionInfoConfig.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "SessionInfo.Icon.Small"));

		TSharedRef<FWorkspaceItem> Group = SessionInfoConfig.WorkspaceGroup.IsValid() ? SessionInfoConfig.WorkspaceGroup.ToSharedRef() : GetInsightsMenuBuilder()->GetInsightsToolsGroup();
		TabSpawnerEntry.SetGroup(Group);
	}

#if !WITH_EDITOR
	const FInsightsMajorTabConfig& MessageLogConfig = InsightsModule.FindMajorTabConfig(FInsightsManagerTabs::InsightsMessageLogTabId);
	if (MessageLogConfig.bIsAvailable)
	{
		FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::InsightsMessageLogTabId,
			FOnSpawnTab::CreateRaw(this, &FInsightsManager::SpawnMessageLogTab))
			.SetDisplayName(MessageLogConfig.TabLabel.IsSet() ? MessageLogConfig.TabLabel.GetValue() : LOCTEXT("InsightsMessageLogTabTitle", "Message Log"))
			.SetTooltipText(MessageLogConfig.TabTooltip.IsSet() ? MessageLogConfig.TabTooltip.GetValue() : LOCTEXT("InsightsMessageLogTooltipText", "Open the Message Log tab."))
			.SetIcon(MessageLogConfig.TabIcon.IsSet() ? MessageLogConfig.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "LogView.Icon.Small"));

		TSharedRef<FWorkspaceItem> Group = MessageLogConfig.WorkspaceGroup.IsValid() ? MessageLogConfig.WorkspaceGroup.ToSharedRef() : GetInsightsMenuBuilder()->GetWindowsGroup();
		TabSpawnerEntry.SetGroup(Group);
	}
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::UnregisterMajorTabs()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::SessionInfoTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::StartPageTabId);
#if !WITH_EDITOR
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::InsightsMessageLogTabId);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FInsightsManager::SpawnStartPageTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);
		//.OnCanCloseTab_Lambda([]() { return false; })
		//.ContentPadding(FMargin(2.0f, 20.0f, 2.0f, 2.0f));

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FInsightsManager::OnStartPageTabClosed));

	// Create the SStartPageWindow widget.
	TSharedRef<SStartPageWindow> Window = SNew(SStartPageWindow);
	DockTab->SetContent(Window);

	AssignStartPageWindow(Window);

	if (!bIsMainTabSet)
	{
		FGlobalTabmanager::Get()->SetMainTab(DockTab);
		bIsMainTabSet = true;
	}

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::OnStartPageTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	RemoveStartPageWindow();

	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FInsightsManager::SpawnSessionInfoTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FInsightsManager::OnSessionInfoTabClosed));

	// Create the SSessionInfoWindow widget.
	TSharedRef<SSessionInfoWindow> Window = SNew(SSessionInfoWindow, DockTab, Args.GetOwnerWindow());
	DockTab->SetContent(Window);

	AssignSessionInfoWindow(Window);

	if (!bIsMainTabSet)
	{
		FGlobalTabmanager::Get()->SetMainTab(DockTab);
		bIsMainTabSet = true;
	}

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::OnSessionInfoTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	RemoveSessionInfoWindow();

	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FInsightsManager::SpawnMessageLogTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("InsightsMessageLogTitle", "Message Log"))
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("InsightsLog")))
			[
				InsightsMessageLog.ToSharedRef()
			]
		];

	return SpawnedTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FInsightsManager::ConnectToStore(const TCHAR* Host, uint32 Port)
{
	using namespace Trace;
	FStoreClient* Client = FStoreClient::Connect(Host, Port);
	StoreClient = TUniquePtr<FStoreClient>(Client);
	return StoreClient.IsValid();
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
	UpdateSessionDuration();

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::UpdateSessionDuration()
{
	if (Session.IsValid())
	{
		double LocalSessionDuration = 0.0;
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			if (bIsAnalysisComplete != Session->IsAnalysisComplete())
			{
				bIsAnalysisComplete = Session->IsAnalysisComplete();
				if (bIsAnalysisComplete)
				{
					SessionAnalysisCompletedEvent.Broadcast();
				}
			}

			LocalSessionDuration = Session->GetDurationSeconds();
		}

		if (LocalSessionDuration != SessionDuration)
		{
			SessionDuration = LocalSessionDuration;
			AnalysisStopwatch.Update();
			AnalysisDuration = AnalysisStopwatch.GetAccumulatedTime();
			AnalysisSpeedFactor = SessionDuration / AnalysisDuration;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::ResetSession(bool bNotify)
{
	if (Session.IsValid())
	{
		Session->Stop(true);
		Session.Reset();

		CurrentTraceId = 0;
		CurrentTraceFilename.Reset();

		if (bNotify)
		{
			OnSessionChanged();
		}
	}

	bIsAnalysisComplete = false;
	SessionDuration = 0.0;
	AnalysisStopwatch.Restart();
	AnalysisDuration = 0.0;
	AnalysisSpeedFactor = 0.0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::OnSessionChanged()
{
	SessionChangedEvent.Broadcast();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::SpawnAndActivateTabs()
{
	// Open Session Info tab.
	if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::SessionInfoTabId))
	{
		FGlobalTabmanager::Get()->TryInvokeTab(FInsightsManagerTabs::SessionInfoTabId);
	}

	// Open Timing Insights tab.
	if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::TimingProfilerTabId))
	{
		FGlobalTabmanager::Get()->TryInvokeTab(FInsightsManagerTabs::TimingProfilerTabId);
	}

	// Open Asset Loading Insights tab.
	if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId))
	{
		FGlobalTabmanager::Get()->TryInvokeTab(FInsightsManagerTabs::LoadingProfilerTabId);
	}

	// Close the existing Networking Insights tabs.
	//for (int32 ReservedId = 0; ReservedId < 10; ++ReservedId)
	{
		FName TabId = FInsightsManagerTabs::NetworkingProfilerTabId;
		//TabId.SetNumber(ReservedId);
		if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
		{
			TSharedPtr<SDockTab> NetworkingProfilerTab;
			while ((NetworkingProfilerTab = FGlobalTabmanager::Get()->FindExistingLiveTab(TabId)).IsValid())
			{
				NetworkingProfilerTab->RequestCloseTab();
			}
		}
	}

	ActivateTimingInsightsTab();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::ActivateTimingInsightsTab()
{
	// Ensure Timing Insights / Timing View is the active tab / view.
	if (TSharedPtr<SDockTab> TimingInsightsTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FInsightsManagerTabs::TimingProfilerTabId))
	{
		TimingInsightsTab->ActivateInParent(ETabActivationCause::SetDirectly);

		//TODO: FTimingProfilerManager::Get()->ActivateWindow();
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

void FInsightsManager::LoadLastLiveSession()
{
	ResetSession();

	if (!StoreClient.IsValid())
	{
		return;
	}

	const int32 SessionCount = StoreClient->GetSessionCount();
	if (SessionCount == 0)
	{
		return;
	}

	const Trace::FStoreClient::FSessionInfo* SessionInfo = StoreClient->GetSessionInfo(SessionCount - 1);
	if (SessionInfo == nullptr)
	{
		return;
	}

	LoadTrace(SessionInfo->GetTraceId());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::LoadTrace(uint32 InTraceId, bool InAutoQuit)
{
	ResetSession();

	if (StoreClient == nullptr)
	{
		if (InAutoQuit)
		{
			RequestEngineExit(AutoQuitMsgOnFail);
		}
		return;
	}

	Trace::FStoreClient::FTraceData TraceData = StoreClient->ReadTrace(InTraceId);
	if (!TraceData)
	{
		if (InAutoQuit)
		{
			RequestEngineExit(AutoQuitMsgOnFail);
		}
		return;
	}

	FString TraceName;
	const Trace::FStoreClient::FTraceInfo* TraceInfo = StoreClient->GetTraceInfoById(InTraceId);
	if (TraceInfo != nullptr)
	{
		FAnsiStringView Name = TraceInfo->GetName();
		TraceName = FString(Name.Len(), Name.GetData());
	}

	Session = AnalysisService->StartAnalysis(*TraceName, MoveTemp(TraceData));

	if (Session)
	{
		CurrentTraceId = InTraceId;
		CurrentTraceFilename = TraceName;
		OnSessionChanged();
	}
	else if (InAutoQuit)
	{
		RequestEngineExit(AutoQuitMsgOnFail);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::LoadTraceFile(const FString& InTraceFilename, bool InAutoQuit)
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	if (!PlatformFile.FileExists(*InTraceFilename))
	{
		const uint32 TraceId = uint32(FCString::Strtoui64(*InTraceFilename, nullptr, 10));
		return LoadTrace(TraceId, InAutoQuit);
	}

	ResetSession();

	Session = AnalysisService->StartAnalysis(*InTraceFilename);

	if (Session)
	{
		CurrentTraceId = 0;
		CurrentTraceFilename = InTraceFilename;
		OnSessionChanged();
	}
	else if(InAutoQuit)
	{
		RequestEngineExit(AutoQuitMsgOnFail);
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
