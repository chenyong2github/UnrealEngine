// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsManager.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/CString.h"
#include "Modules/ModuleManager.h"
#include "Templates/UniquePtr.h"
#include "Trace/StoreClient.h"
#include "TraceServices/Model/NetProfiler.h"

// Insights
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/LoadingProfiler/LoadingProfilerManager.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/SStartPageWindow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "InsightsManager"

const FName FInsightsManagerTabs::StartPageTabId(TEXT("StartPage"));
const FName FInsightsManagerTabs::SessionInfoTabId(TEXT("SessionInfo"));
const FName FInsightsManagerTabs::TimingProfilerTabId(TEXT("TimingProfiler"));
const FName FInsightsManagerTabs::LoadingProfilerTabId(TEXT("LoadingProfiler"));
const FName FInsightsManagerTabs::NetworkingProfilerTabId(TEXT("NetworkingProfiler"));

TSharedPtr<FInsightsManager> FInsightsManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsManager::FInsightsManager(TSharedRef<Trace::IAnalysisService> InTraceAnalysisService,
								   TSharedRef<Trace::IModuleService> InTraceModuleService)
	: AnalysisService(InTraceAnalysisService)
	, ModuleService(InTraceModuleService)
	, StoreDir()
	, StoreClient()
	, CommandList(new FUICommandList())
	, ActionManager(this)
	, Settings()
	, bIsDebugInfoEnabled(false)
	, bIsNetworkingProfilerAvailable(false)
#if WITH_EDITOR
	, bShouldOpenAnalysisInSeparateProcess(false)
#else
	, bShouldOpenAnalysisInSeparateProcess(true)
#endif
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::PostConstructor()
{
	// Register tick functions.
	OnTick = FTickerDelegate::CreateSP(this, &FInsightsManager::Tick);
	OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

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
	ResetSession(false);

	FInsightsCommands::Unregister();

	// Unregister tick function.
	FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);
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

bool FInsightsManager::ConnectToStore(const TCHAR* Host, uint32 Port)
{
	using namespace Trace;
	FStoreClient* Client = FStoreClient::Connect(Host, Port);
	StoreClient = TUniquePtr<FStoreClient>(Client);
	return StoreClient.IsValid();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FInsightsManager::Tick(float DeltaTime)
{
	if (!bIsNetworkingProfilerAvailable)
	{
		uint32 NetTraceVersion = 0;

		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const Trace::INetProfilerProvider& NetProfilerProvider = Trace::ReadNetProfilerProvider(*Session.Get());
			NetTraceVersion = NetProfilerProvider.GetNetTraceVersion();
		}

		if (NetTraceVersion > 0)
		{
			bIsNetworkingProfilerAvailable = true;

			if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::NetworkingProfilerTabId))
			{
				FGlobalTabmanager::Get()->InvokeTab(FInsightsManagerTabs::NetworkingProfilerTabId);
				FGlobalTabmanager::Get()->InvokeTab(FInsightsManagerTabs::NetworkingProfilerTabId);
			}

			ActivateTimingInsightsTab();
		}
	}

	return true;
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
		bIsNetworkingProfilerAvailable = false;

		if (bNotify)
		{
			OnSessionChanged();
		}
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

	if (TSharedPtr<FLoadingProfilerManager> LoadingProfilerManager = FLoadingProfilerManager::Get())
	{
		// FIXME: make LoadingProfilerManager to register to SessionChangedEvent instead
		LoadingProfilerManager->OnSessionChanged();
	}

	if (TSharedPtr<FNetworkingProfilerManager> NetworkingProfilerManager = FNetworkingProfilerManager::Get())
	{
		// FIXME: make NetworkingProfilerManager to register to SessionChangedEvent instead
		NetworkingProfilerManager->OnSessionChanged();
	}

	SessionChangedEvent.Broadcast();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::SpawnAndActivateTabs()
{
	// Open Session Info tab.
	if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::SessionInfoTabId))
	{
		FGlobalTabmanager::Get()->InvokeTab(FInsightsManagerTabs::SessionInfoTabId);
	}

	// Open Timing Insights tab.
	if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::TimingProfilerTabId))
	{
		FGlobalTabmanager::Get()->InvokeTab(FInsightsManagerTabs::TimingProfilerTabId);
	}

	// Open Asset Loading Insights tab.
	if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId))
	{
		FGlobalTabmanager::Get()->InvokeTab(FInsightsManagerTabs::LoadingProfilerTabId);
	}

	// Close the existing Networking Insights tabs.
	if (FGlobalTabmanager::Get()->HasTabSpawner(FInsightsManagerTabs::NetworkingProfilerTabId))
	{
		TSharedPtr<SDockTab> NetworkingProfilerTab;
		while ((NetworkingProfilerTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FInsightsManagerTabs::NetworkingProfilerTabId)).IsValid())
		{
			NetworkingProfilerTab->RequestCloseTab();
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

void FInsightsManager::LoadTrace(uint32 InTraceId)
{
	ResetSession();

	if (StoreClient == nullptr)
	{
		return;
	}

	Trace::FStoreClient::FTraceData TraceData = StoreClient->ReadTrace(InTraceId);
	if (!TraceData)
	{
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
		bIsNetworkingProfilerAvailable = false;
		OnSessionChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsManager::LoadTraceFile(const FString& InTraceFilename)
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	if (!PlatformFile.FileExists(*InTraceFilename))
	{
		const uint32 TraceId = uint32(FCString::Strtoui64(*InTraceFilename, nullptr, 10));
		return LoadTrace(TraceId);
	}

	ResetSession();

	Session = AnalysisService->StartAnalysis(*InTraceFilename);

	if (Session)
	{
		CurrentTraceId = 0;
		CurrentTraceFilename = InTraceFilename;
		bIsNetworkingProfilerAvailable = false;
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
