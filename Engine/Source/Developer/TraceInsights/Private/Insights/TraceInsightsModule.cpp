// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceInsightsModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Modules/ModuleManager.h"
#include "Trace/StoreClient.h"
#include "Trace/StoreService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/LayoutService.h"
#include "Widgets/SWidget.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/LoadingProfiler/LoadingProfilerManager.h"
#include "Insights/LoadingProfiler/Widgets/SLoadingProfilerWindow.h"
#include "Insights/Log.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerManager.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerWindow.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/SSessionInfoWindow.h"
#include "Insights/Widgets/SStartPageWindow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

DEFINE_LOG_CATEGORY(TraceInsights);

IMPLEMENT_MODULE(FTraceInsightsModule, TraceInsights);

FString FTraceInsightsModule::UnrealInsightsLayoutIni;

////////////////////////////////////////////////////////////////////////////////////////////////////

static TSharedPtr<SDockTab> NeverReuse(const FTabId&)
{
	return TSharedPtr<SDockTab>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTraceInsightsModule
////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::StartupModule()
{
	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	TraceAnalysisService = TraceServicesModule.GetAnalysisService();
	TraceModuleService = TraceServicesModule.GetModuleService();

	FInsightsStyle::Initialize();

	FInsightsManager::Initialize(TraceAnalysisService.ToSharedRef(), TraceModuleService.ToSharedRef());
	FTimingProfilerManager::Initialize();
	FLoadingProfilerManager::Initialize();
	FNetworkingProfilerManager::Initialize();

	UnrealInsightsLayoutIni = FPaths::GetPath(GEngineIni) + "/UnrealInsightsLayout.ini";
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::ShutdownModule()
{
	if (PersistentLayout.IsValid())
	{
		// Save application layout.
		FLayoutSaveRestore::SaveToConfig(UnrealInsightsLayoutIni, PersistentLayout.ToSharedRef());
		GConfig->Flush(false, UnrealInsightsLayoutIni);
	}

	UnregisterTabSpawners();

	if (FNetworkingProfilerManager::Get().IsValid())
	{
		// Shutdown the NetworkingProfiler (Networking Insights) manager.
		FNetworkingProfilerManager::Get()->Shutdown();
	}

	if (FLoadingProfilerManager::Get().IsValid())
	{
		// Shutdown the LoadingProfiler (Asset Loading Insights) manager.
		FLoadingProfilerManager::Get()->Shutdown();
	}

	if (FTimingProfilerManager::Get().IsValid())
	{
		// Shutdown the TimingProfiler (Timing Insights) manager.
		FTimingProfilerManager::Get()->Shutdown();
	}

	if (FInsightsManager::Get().IsValid())
	{
		// Shutdown the main manager.
		FInsightsManager::Get()->Shutdown();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::CreateDefaultStore()
{
	const FString StoreDir = FPaths::ProjectSavedDir() / TEXT("TraceSessions");

	FInsightsManager::Get()->SetStoreDir(StoreDir);

	// Create the Store Service.
	Trace::FStoreService::FDesc StoreServiceDesc;
	StoreServiceDesc.StoreDir = *StoreDir;
	StoreServiceDesc.RecorderPort = 1980;
	StoreServiceDesc.ThreadCount = 2;
	StoreService = TUniquePtr<Trace::FStoreService>(Trace::FStoreService::Create(StoreServiceDesc));

	if (StoreService.IsValid())
	{
		ConnectToStore(TEXT("127.0.0.1"), StoreService->GetPort());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Trace::FStoreClient* FTraceInsightsModule::GetStoreClient()
{
	return FInsightsManager::Get()->GetStoreClient();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTraceInsightsModule::ConnectToStore(const TCHAR* InStoreHost, uint32 InStorePort)
{
	return FInsightsManager::Get()->ConnectToStore(InStoreHost, InStorePort);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::RegisterTabSpawners()
{
	TSharedRef<FWorkspaceItem> ToolsCategory = WorkspaceMenu::GetMenuStructure().GetToolsCategory();

	const FInsightsMajorTabConfig& StartPageConfig = FindMajorTabConfig(FInsightsManagerTabs::StartPageTabId);
	if (StartPageConfig.bIsAvailable)
	{
		// Register tab spawner for the Start Page.
		FTabSpawnerEntry& StartPageTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::StartPageTabId,
			FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnStartPageTab))
			.SetDisplayName(StartPageConfig.TabLabel.IsSet() ? StartPageConfig.TabLabel.GetValue() : NSLOCTEXT("FTraceInsightsModule", "StartPageTabTitle", "Unreal Insights"))
			.SetTooltipText(StartPageConfig.TabTooltip.IsSet() ? StartPageConfig.TabTooltip.GetValue() : NSLOCTEXT("FTraceInsightsModule", "StartPageTooltipText", "Open the start page for Unreal Insights."))
			.SetIcon(StartPageConfig.TabIcon.IsSet() ? StartPageConfig.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "StartPage.Icon.Small"));

		StartPageTabSpawnerEntry.SetGroup(StartPageConfig.WorkspaceGroup.IsValid() ? StartPageConfig.WorkspaceGroup.ToSharedRef() : ToolsCategory);
	}

	const FInsightsMajorTabConfig& SessionInfoConfig = FindMajorTabConfig(FInsightsManagerTabs::SessionInfoTabId);
	if (SessionInfoConfig.bIsAvailable)
	{
		// Register tab spawner for the Session Info.
		FTabSpawnerEntry& SessionInfoTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::SessionInfoTabId,
			FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnSessionInfoTab))
			.SetDisplayName(SessionInfoConfig.TabLabel.IsSet() ? SessionInfoConfig.TabLabel.GetValue() : NSLOCTEXT("FTraceInsightsModule", "SessionInfoTabTitle", "Session Info"))
			.SetTooltipText(SessionInfoConfig.TabTooltip.IsSet() ? SessionInfoConfig.TabTooltip.GetValue() : NSLOCTEXT("FTraceInsightsModule", "SessionInfoTooltipText", "Open the Session Info tab."))
			.SetIcon(SessionInfoConfig.TabIcon.IsSet() ? SessionInfoConfig.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "SessionInfo.Icon.Small"));

		SessionInfoTabSpawnerEntry.SetGroup(SessionInfoConfig.WorkspaceGroup.IsValid() ? SessionInfoConfig.WorkspaceGroup.ToSharedRef() : ToolsCategory);
	}

	const FInsightsMajorTabConfig& TimingProfilerConfig = FindMajorTabConfig(FInsightsManagerTabs::TimingProfilerTabId);
	if (TimingProfilerConfig.bIsAvailable)
	{
		// Register tab spawner for the Timing Insights.
		FTabSpawnerEntry& TimingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::TimingProfilerTabId,
			FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnTimingProfilerTab))
			.SetDisplayName(TimingProfilerConfig.TabLabel.IsSet() ? TimingProfilerConfig.TabLabel.GetValue() : NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTabTitle", "Timing Insights"))
			.SetTooltipText(TimingProfilerConfig.TabTooltip.IsSet() ? TimingProfilerConfig.TabTooltip.GetValue() : NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTooltipText", "Open the Timing Insights tab."))
			.SetIcon(TimingProfilerConfig.TabIcon.IsSet() ? TimingProfilerConfig.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimingProfiler.Icon.Small"));

		TimingProfilerTabSpawnerEntry.SetGroup(TimingProfilerConfig.WorkspaceGroup.IsValid() ? TimingProfilerConfig.WorkspaceGroup.ToSharedRef() : ToolsCategory);
	}

	const FInsightsMajorTabConfig& LoadingProfilerConfig = FindMajorTabConfig(FInsightsManagerTabs::LoadingProfilerTabId);
	if (LoadingProfilerConfig.bIsAvailable)
	{
		// Register tab spawner for the Asset Loading Insights.
		FTabSpawnerEntry& LoadingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId,
			FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnLoadingProfilerTab))
			.SetDisplayName(LoadingProfilerConfig.TabLabel.IsSet() ? LoadingProfilerConfig.TabLabel.GetValue() : NSLOCTEXT("FTraceInsightsModule", "LoadingProfilerTabTitle", "Asset Loading Insights"))
			.SetTooltipText(LoadingProfilerConfig.TabTooltip.IsSet() ? LoadingProfilerConfig.TabTooltip.GetValue() : NSLOCTEXT("FTraceInsightsModule", "LoadingProfilerTooltipText", "Open the Asset Loading Insights tab."))
			.SetIcon(LoadingProfilerConfig.TabIcon.IsSet() ? LoadingProfilerConfig.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "LoadingProfiler.Icon.Small"));

		LoadingProfilerTabSpawnerEntry.SetGroup(LoadingProfilerConfig.WorkspaceGroup.IsValid() ? LoadingProfilerConfig.WorkspaceGroup.ToSharedRef() : ToolsCategory);
	}

	const FInsightsMajorTabConfig& NetworkingProfilerConfig = FindMajorTabConfig(FInsightsManagerTabs::NetworkingProfilerTabId);
	if (NetworkingProfilerConfig.bIsAvailable)
	{
		// Register tab spawner for the Networking Insights.
		FTabSpawnerEntry& NetworkingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::NetworkingProfilerTabId,
			FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnNetworkingProfilerTab))
			.SetReuseTabMethod(FOnFindTabToReuse::CreateStatic(&NeverReuse))
			.SetDisplayName(NetworkingProfilerConfig.TabLabel.IsSet() ? NetworkingProfilerConfig.TabLabel.GetValue() : NSLOCTEXT("FTraceInsightsModule", "NetworkingProfilerTabTitle", "Networking Insights"))
			.SetTooltipText(NetworkingProfilerConfig.TabTooltip.IsSet() ? NetworkingProfilerConfig.TabTooltip.GetValue() : NSLOCTEXT("FTraceInsightsModule", "NetworkingProfilerTooltipText", "Open the Networking Insights tab."))
			.SetIcon(NetworkingProfilerConfig.TabIcon.IsSet() ? NetworkingProfilerConfig.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "NetworkingProfiler.Icon.Small"));

		NetworkingProfilerTabSpawnerEntry.SetGroup(NetworkingProfilerConfig.WorkspaceGroup.IsValid() ? NetworkingProfilerConfig.WorkspaceGroup.ToSharedRef() : ToolsCategory);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::UnregisterTabSpawners()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::NetworkingProfilerTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::TimingProfilerTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::SessionInfoTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::StartPageTabId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::CreateSessionBrowser(bool bAllowDebugTools, bool bSingleProcess)
{
	FInsightsManager::Get()->SetOpenAnalysisInSeparateProcess(!bSingleProcess);

	RegisterTabSpawners();

	TSharedRef<FWorkspaceItem> ToolsCategory = WorkspaceMenu::GetMenuStructure().GetToolsCategory();

	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);

	TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout("TraceSessionBrowserLayout_v1.0");

	if (!bSingleProcess)
	{
		constexpr float WindowWidth = 920.0f;
		constexpr float WindowHeight = 665.0f;

		DefaultLayout->AddArea
		(
			FTabManager::NewArea(WindowWidth * DPIScaleFactor, WindowHeight * DPIScaleFactor)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FInsightsManagerTabs::StartPageTabId, ETabState::OpenedTab)
				//->SetForegroundTab(FTabId(FInsightsManagerTabs::StartPageTabId))
				//->SetHideTabWell(true)
			)
		);
	}
	else
	{
		constexpr float WindowWidth = 1280.0f;
		constexpr float WindowHeight = 720.0f;

		DefaultLayout->AddArea
		(
			FTabManager::NewArea(WindowWidth * DPIScaleFactor, WindowHeight * DPIScaleFactor)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FInsightsManagerTabs::StartPageTabId, ETabState::OpenedTab)
				->AddTab(FInsightsManagerTabs::SessionInfoTabId, ETabState::ClosedTab)
				->AddTab(FInsightsManagerTabs::TimingProfilerTabId, ETabState::ClosedTab)
				->AddTab(FInsightsManagerTabs::LoadingProfilerTabId, ETabState::ClosedTab)
				->AddTab(FInsightsManagerTabs::NetworkingProfilerTabId, ETabState::ClosedTab)
				->SetForegroundTab(FTabId(FInsightsManagerTabs::StartPageTabId))
				//->SetHideTabWell(true)
			)
		);
	}

	AddAreaForWidgetReflector(DefaultLayout, bAllowDebugTools);

	// Restore application layout.
	PersistentLayout = FLayoutSaveRestore::LoadFromConfig(UnrealInsightsLayoutIni, DefaultLayout);
	FGlobalTabmanager::Get()->RestoreFrom(PersistentLayout.ToSharedRef(), TSharedPtr<SWindow>());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::CreateSessionViewer(bool bAllowDebugTools)
{
	RegisterTabSpawners();

#if !WITH_EDITOR
	TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout("UnrealInsightsLayout_v1.0");

	AddAreaForSessionViewer(DefaultLayout);

	AddAreaForWidgetReflector(DefaultLayout, bAllowDebugTools);

	// Restore application layout.
	PersistentLayout = FLayoutSaveRestore::LoadFromConfig(UnrealInsightsLayoutIni, DefaultLayout);
	FGlobalTabmanager::Get()->RestoreFrom(PersistentLayout.ToSharedRef(), TSharedPtr<SWindow>());
#endif 
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::AddAreaForSessionViewer(TSharedRef<FTabManager::FLayout> Layout)
{
	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);

	// Create area for the main window.
	Layout->AddArea
	(
		FTabManager::NewArea(1280.f * DPIScaleFactor, 720.0f * DPIScaleFactor)
		->Split
		(
			FTabManager::NewStack()
			->AddTab(FInsightsManagerTabs::StartPageTabId, ETabState::ClosedTab)
#if WITH_EDITOR
			// In editor, we default to all tabs closed.
			->AddTab(FInsightsManagerTabs::SessionInfoTabId, ETabState::ClosedTab)
			->AddTab(FInsightsManagerTabs::TimingProfilerTabId, ETabState::ClosedTab)
			->AddTab(FInsightsManagerTabs::LoadingProfilerTabId, ETabState::ClosedTab)
#else
			->AddTab(FInsightsManagerTabs::SessionInfoTabId, ETabState::OpenedTab)
			->AddTab(FInsightsManagerTabs::TimingProfilerTabId, ETabState::OpenedTab)
			->AddTab(FInsightsManagerTabs::LoadingProfilerTabId, ETabState::OpenedTab)
#endif
			->AddTab(FInsightsManagerTabs::NetworkingProfilerTabId, ETabState::ClosedTab)
			->SetForegroundTab(FTabId(FInsightsManagerTabs::TimingProfilerTabId))
		)
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::AddAreaForWidgetReflector(TSharedRef<FTabManager::FLayout> Layout, bool bAllowDebugTools)
{
	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);

	// Create area and tab for Slate's WidgetReflector.
	Layout->AddArea
	(
		FTabManager::NewArea(600.0f * DPIScaleFactor, 600.0f * DPIScaleFactor)
		->SetWindow(FVector2D(10.0f * DPIScaleFactor, 10.0f * DPIScaleFactor), false)
		->Split
		(
			FTabManager::NewStack()->AddTab("WidgetReflector", bAllowDebugTools ? ETabState::OpenedTab : ETabState::ClosedTab)
		)
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::ShutdownUserInterface()
{
	check(PersistentLayout.IsValid());

	// Save application layout.
	FLayoutSaveRestore::SaveToConfig(UnrealInsightsLayoutIni, PersistentLayout.ToSharedRef());
	GConfig->Flush(false, UnrealInsightsLayoutIni);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::RegisterMajorTabConfig(const FName& InMajorTabId, const FInsightsMajorTabConfig& InConfig)
{
	TabConfigs.Add(InMajorTabId, InConfig);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::UnregisterMajorTabConfig(const FName& InMajorTabId)
{
	TabConfigs.Remove(InMajorTabId);
}


////////////////////////////////////////////////////////////////////////////////////////////////////

FOnRegisterMajorTabExtensions& FTraceInsightsModule::OnRegisterMajorTabExtension(const FName& InMajorTabId)
{
	return MajorTabExtensionDelegates.FindOrAdd(InMajorTabId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FInsightsMajorTabConfig& FTraceInsightsModule::FindMajorTabConfig(const FName& InMajorTabId) const
{
	const FInsightsMajorTabConfig* FoundConfig = TabConfigs.Find(InMajorTabId);
	if (FoundConfig != nullptr)
	{
		return *FoundConfig;
	}

	static FInsightsMajorTabConfig DefaultConfig;
	return DefaultConfig;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FOnRegisterMajorTabExtensions* FTraceInsightsModule::FindMajorTabLayoutExtension(const FName& InMajorTabId) const
{
	return MajorTabExtensionDelegates.Find(InMajorTabId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FString& FTraceInsightsModule::GetUnrealInsightsLayoutIni()
{
	return UnrealInsightsLayoutIni;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::SetUnrealInsightsLayoutIni(const FString& InIniPath)
{
	UnrealInsightsLayoutIni = InIniPath;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<const Trace::IAnalysisSession> FTraceInsightsModule::GetAnalysisSession() const
{
	return FInsightsManager::Get()->GetSession();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::StartAnalysisForTrace(uint32 InTraceId)
{
	if (InTraceId != 0)
	{
		FInsightsManager::Get()->LoadTrace(InTraceId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::StartAnalysisForLastLiveSession()
{
	FInsightsManager::Get()->LoadLastLiveSession();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::StartAnalysisForTraceFile(const TCHAR* InTraceFile)
{
	if (InTraceFile != nullptr)
	{
		FInsightsManager::Get()->LoadTraceFile(FString(InTraceFile));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTraceInsightsModule::SpawnStartPageTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);
		//.OnCanCloseTab_Lambda([]() { return false; })
		//.ContentPadding(FMargin(2.0f, 20.0f, 2.0f, 2.0f));

	// Create the Start Page widget.
	TSharedRef<SStartPageWindow> Window = SNew(SStartPageWindow);
	DockTab->SetContent(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTraceInsightsModule::SpawnSessionInfoTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Create the Session Info widget.
	TSharedRef<SSessionInfoWindow> Window = SNew(SSessionInfoWindow);
	DockTab->SetContent(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTraceInsightsModule::SpawnTimingProfilerTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle Timing profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FTraceInsightsModule::OnTimingProfilerTabBeingClosed));

	// Create the STimingProfilerWindow widget.
	TSharedRef<STimingProfilerWindow> Window = SNew(STimingProfilerWindow, DockTab, Args.GetOwnerWindow());
	FTimingProfilerManager::Get()->AssignProfilerWindow(Window);
	DockTab->SetContent(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::OnTimingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTraceInsightsModule::SpawnLoadingProfilerTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle I/O profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FTraceInsightsModule::OnLoadingProfilerTabBeingClosed));

	// Create the SLoadingProfilerWindow widget.
	TSharedRef<SLoadingProfilerWindow> Window = SNew(SLoadingProfilerWindow, DockTab, Args.GetOwnerWindow());
	FLoadingProfilerManager::Get()->AssignProfilerWindow(Window);
	DockTab->SetContent(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::OnLoadingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTraceInsightsModule::SpawnNetworkingProfilerTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle I/O profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FTraceInsightsModule::OnNetworkingProfilerTabBeingClosed));

	// Create the SNetworkingProfilerWindow widget.
	TSharedRef<SNetworkingProfilerWindow> Window = SNew(SNetworkingProfilerWindow, DockTab, Args.GetOwnerWindow());
	FNetworkingProfilerManager::Get()->AddProfilerWindow(Window);
	DockTab->SetContent(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::OnNetworkingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	TSharedRef<SNetworkingProfilerWindow> Window = StaticCastSharedRef<SNetworkingProfilerWindow>(TabBeingClosed->GetContent());
	FNetworkingProfilerManager::Get()->RemoveProfilerWindow(Window);

	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
