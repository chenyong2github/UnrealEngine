// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/SessionService.h"
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
#include "Insights/NetworkingProfiler/NetworkingProfilerManager.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerWindow.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/SSessionInfoWindow.h"
#include "Insights/Widgets/SStartPageWindow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Implements the Trace Insights module.
 */
class FTraceInsightsModule : public IUnrealInsightsModule
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	virtual void CreateSessionBrowser(bool bAllowDebugTools, bool bSingleProcess) override;
	virtual void CreateSessionViewer(bool bAllowDebugTools) override;
	virtual void StartAnalysisForTraceFile(const TCHAR* InTraceFile) override;
	virtual void StartAnalysisForSession(const TCHAR* InSessionId) override;
	virtual void ShutdownUserInterface() override;

protected:
	void AddAreaForSessionViewer(TSharedRef<FTabManager::FLayout> Layout);
	void AddAreaForWidgetReflector(TSharedRef<FTabManager::FLayout> Layout, bool bAllowDebugTools);

	/** Callback called when a major tab is closed. */
	void OnTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Callback called when the Timing Profiler major tab is closed. */
	void OnTimingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Callback called when the Loading Profiler major tab is closed. */
	void OnLoadingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Callback called when the Networking Profiler major tab is closed. */
	void OnNetworkingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Start Page */
	TSharedRef<SDockTab> SpawnStartPageTab(const FSpawnTabArgs& Args);

	/** Session Info */
	TSharedRef<SDockTab> SpawnSessionInfoTab(const FSpawnTabArgs& Args);

	/** Timing Profiler */
	TSharedRef<SDockTab> SpawnTimingProfilerTab(const FSpawnTabArgs& Args);

	/** Loading Profiler */
	TSharedRef<SDockTab> SpawnLoadingProfilerTab(const FSpawnTabArgs& Args);

	/** Networking Profiler */
	TSharedRef<SDockTab> SpawnNetworkingProfilerTab(const FSpawnTabArgs& Args);

#if WITH_EDITOR
	/** Handle exit */
	void HandleExit();
#endif

protected:
	TSharedPtr<FTabManager::FLayout> PersistentLayout;
	bool bBrowserMode;
	FString UnrealInsightsLayoutIni;

	TSharedPtr<Trace::IAnalysisService> TraceAnalysisService;
	TSharedPtr<Trace::ISessionService> TraceSessionService;
	TSharedPtr<Trace::IModuleService> TraceModuleService;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FTraceInsightsModule, TraceInsights);

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
	TraceSessionService = TraceServicesModule.GetSessionService();
	TraceModuleService = TraceServicesModule.GetModuleService();

	FInsightsStyle::Initialize();

	FInsightsManager::Initialize(TraceAnalysisService.ToSharedRef(), TraceSessionService.ToSharedRef(), TraceModuleService.ToSharedRef());
	FTimingProfilerManager::Initialize();
	FLoadingProfilerManager::Initialize();
	FNetworkingProfilerManager::Initialize();

	//////////////////////////////////////////////////

	// Register tab spawner for the Start Page.
	auto& StartPageTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::StartPageTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnStartPageTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "StartPageTabTitle", "Unreal Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "StartPageTooltipText", "Open the start page for Unreal Insights."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "StartPage.Icon.Small"));

#if WITH_EDITOR
	StartPageTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory());
#else
	StartPageTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
#endif

	//////////////////////////////////////////////////

	// Register tab spawner for the Session Info.
	auto& SessionInfoTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::SessionInfoTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnSessionInfoTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "SessionInfoTabTitle", "Session Info"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "SessionInfoTooltipText", "Open the Session Info tab."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "SessionInfo.Icon.Small"));

#if WITH_EDITOR
	SessionInfoTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory());
#else
	SessionInfoTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
#endif

	//////////////////////////////////////////////////

	// Register tab spawner for the Timing Insights.
	auto& TimingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::TimingProfilerTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnTimingProfilerTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTabTitle", "Timing Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTooltipText", "Open the Timing Insights tab."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimingProfiler.Icon.Small"));

#if WITH_EDITOR
	TimingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory());
#else
	TimingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
#endif

	//////////////////////////////////////////////////

	// Register tab spawner for the Asset Loading Insights.
	auto& LoadingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnLoadingProfilerTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "LoadingProfilerTabTitle", "Asset Loading Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "LoadingProfilerTooltipText", "Open the Asset Loading Insights tab."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "LoadingProfiler.Icon.Small"));

#if WITH_EDITOR
	LoadingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory());
#else
	LoadingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
#endif

	//////////////////////////////////////////////////

	// Register tab spawner for the Networking Insights.
	auto& NetworkingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::NetworkingProfilerTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnNetworkingProfilerTab))
		.SetReuseTabMethod(FOnFindTabToReuse::CreateStatic(&NeverReuse))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "NetworkingProfilerTabTitle", "Networking Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "NetworkingProfilerTooltipText", "Open the Networking Insights tab."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "NetworkingProfiler.Icon.Small"));

#if WITH_EDITOR
	NetworkingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory());
#else
	NetworkingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
#endif

	//////////////////////////////////////////////////

#if WITH_EDITOR
	if (TraceSessionService.IsValid())
	{
		TraceSessionService->StartRecorderServer();
	}

	// In editor, load the layout and display it here. In the standalone tool this is done through CreateSessionBrowser or CreateSessionViewer.
	TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout("UnrealInsightsLayout_v1.0");
	AddAreaForSessionViewer(DefaultLayout);
	PersistentLayout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, DefaultLayout);
	FGlobalTabmanager::Get()->RestoreFrom(PersistentLayout.ToSharedRef(), TSharedPtr<SWindow>());

	FCoreDelegates::OnExit.AddRaw(this, &FTraceInsightsModule::HandleExit);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::ShutdownModule()
{
#if WITH_EDITOR
	// Save application layout.
	FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, PersistentLayout.ToSharedRef());
	GConfig->Flush(false, GEditorLayoutIni);
#endif

#if !WITH_EDITOR
	if (TraceSessionService.IsValid())
	{
		TraceSessionService->StopRecorderServer();
	}
#endif

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::NetworkingProfilerTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::TimingProfilerTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::SessionInfoTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::StartPageTabId);

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

#if WITH_EDITOR
void FTraceInsightsModule::HandleExit()
{
	// In editor, as module lifetimes are different, we need to shut down the recorder service before
	// threads get killed in the shutdown process otherwise we will hang forever waiting on the recorder server
	if (TraceSessionService.IsValid())
	{
		TraceSessionService->StopRecorderServer();
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::CreateSessionBrowser(bool bAllowDebugTools, bool bSingleProcess)
{
	bBrowserMode = true;
	FInsightsManager::Get()->SetOpenAnalysisInSeparateProcess(!bSingleProcess);

	if (TraceSessionService.IsValid())
	{
		TraceSessionService->StartRecorderServer();
	}

	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);

	TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout("TraceSessionBrowserLayout_v1.0");

	const float WindowWidth  = bSingleProcess ? 1280.0f : 920.0f;
	const float WindowHeight = bSingleProcess ? 720.0f : 664.0f;

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

	AddAreaForWidgetReflector(DefaultLayout, bAllowDebugTools);

	// Restore application layout.
	UnrealInsightsLayoutIni = FPaths::GetPath(GEngineIni) + "/UnrealInsightsLayout.ini";
	PersistentLayout = FLayoutSaveRestore::LoadFromConfig(UnrealInsightsLayoutIni, DefaultLayout);
	FGlobalTabmanager::Get()->RestoreFrom(PersistentLayout.ToSharedRef(), TSharedPtr<SWindow>());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::CreateSessionViewer(bool bAllowDebugTools)
{
	bBrowserMode = false;

	TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout("UnrealInsightsLayout_v1.0");

	AddAreaForSessionViewer(DefaultLayout);
	AddAreaForWidgetReflector(DefaultLayout, bAllowDebugTools);

	// Restore application layout.
	UnrealInsightsLayoutIni = FPaths::GetPath(GEngineIni) + "/UnrealInsightsLayout.ini";
	PersistentLayout = FLayoutSaveRestore::LoadFromConfig(UnrealInsightsLayoutIni, DefaultLayout);
	FGlobalTabmanager::Get()->RestoreFrom(PersistentLayout.ToSharedRef(), TSharedPtr<SWindow>());
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
#else
			->AddTab(FInsightsManagerTabs::SessionInfoTabId, ETabState::OpenedTab)
			->AddTab(FInsightsManagerTabs::TimingProfilerTabId, ETabState::OpenedTab)
#endif
			->AddTab(FInsightsManagerTabs::LoadingProfilerTabId, ETabState::ClosedTab)
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

void FTraceInsightsModule::StartAnalysisForTraceFile(const TCHAR* InTraceFile)
{
	if (InTraceFile != nullptr)
	{
		FInsightsManager::Get()->LoadTraceFile(FString(InTraceFile));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::StartAnalysisForSession(const TCHAR* InSessionId)
{
	if (InSessionId != nullptr)
	{
		//TODO: FInsightsManager::Get()->LoadSession(FString(InSessionId));
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
