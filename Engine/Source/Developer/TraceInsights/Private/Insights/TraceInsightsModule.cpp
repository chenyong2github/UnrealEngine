// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/SessionService.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/LoadingProfiler/LoadingProfilerManager.h"
#include "Insights/LoadingProfiler/Widgets/SLoadingProfilerWindow.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerManager.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerWindow.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/SStartPageWindow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Implements the Trace Insights module (TimingProfiler, LoadingProfiler, etc.).
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

	virtual void OnNewLayout(TSharedRef<FTabManager::FLayout> NewLayout) override;
	virtual void OnLayoutRestored(TSharedPtr<FTabManager> TabManager) override;

protected:
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

	/** Timing Profiler */
	TSharedRef<SDockTab> SpawnTimingProfilerTab(const FSpawnTabArgs& Args);

	/** Loading Profiler */
	TSharedRef<SDockTab> SpawnLoadingProfilerTab(const FSpawnTabArgs& Args);

	/** Networking Profiler */
	TSharedRef<SDockTab> SpawnNetworkingProfilerTab(const FSpawnTabArgs& Args);

protected:
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

	// Starts the Trace Recorder service.
	TraceSessionService->StartRecorderServer();

	FInsightsStyle::Initialize();

	FInsightsManager::Initialize(TraceAnalysisService.ToSharedRef(), TraceSessionService.ToSharedRef(), TraceModuleService.ToSharedRef());
	FTimingProfilerManager::Initialize();
	FLoadingProfilerManager::Initialize();
	FNetworkingProfilerManager::Initialize();

	// Register tab spawner for the Start Page.
	auto& StartPageTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::StartPageTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnStartPageTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "StartPageTabTitle", "Unreal Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "StartPageTooltipText", "Open the start page for Unreal Insights."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "StartPage.Icon.Small"));

//#if WITH_EDITOR
//	StartPageTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
//#else
//	StartPageTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
//#endif

	// Register tab spawner for the Timing Insights.
	auto& TimingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::TimingProfilerTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnTimingProfilerTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTabTitle", "Timing Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTooltipText", "Open the Timing Insights tab."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimingProfiler.Icon.Small"));

//#if WITH_EDITOR
//	TimingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
//#else
//	TimingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
//#endif

	// Register tab spawner for the Asset Loading Insights.
	auto& LoadingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnLoadingProfilerTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "LoadingProfilerTabTitle", "Asset Loading Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "LoadingProfilerTooltipText", "Open the Asset Loading Insights tab."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "LoadingProfiler.Icon.Small"));

//#if WITH_EDITOR
//	LoadingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
//#else
//	LoadingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
//#endif

	// Register tab spawner for the Networking Insights.
	auto& NetworkingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::NetworkingProfilerTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnNetworkingProfilerTab))
		.SetReuseTabMethod(FOnFindTabToReuse::CreateStatic(&NeverReuse))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "NetworkingProfilerTabTitle", "Networking Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "NetworkingProfilerTooltipText", "Open the Networking Insights tab."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "NetworkingProfiler.Icon.Small"));

//#if WITH_EDITOR
//	NetworkingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
//#else
//	NetworkingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
//#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::ShutdownModule()
{
	TraceSessionService->StopRecorderServer();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::NetworkingProfilerTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::LoadingProfilerTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::TimingProfilerTabId);
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

void FTraceInsightsModule::OnNewLayout(TSharedRef<FTabManager::FLayout> NewLayout)
{
	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);
	NewLayout->AddArea
	(
		FTabManager::NewArea(1280.f * DPIScaleFactor, 720.0f * DPIScaleFactor)
		->Split
		(
			FTabManager::NewStack()
			->AddTab(FInsightsManagerTabs::StartPageTabId, ETabState::OpenedTab)
			->AddTab(FInsightsManagerTabs::TimingProfilerTabId, ETabState::ClosedTab)
			->AddTab(FInsightsManagerTabs::LoadingProfilerTabId, ETabState::ClosedTab)
			->AddTab(FInsightsManagerTabs::NetworkingProfilerTabId, ETabState::ClosedTab)
			->SetForegroundTab(FTabId(FInsightsManagerTabs::StartPageTabId))
		)
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::OnLayoutRestored(TSharedPtr<FTabManager> TabManager)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTraceInsightsModule::SpawnStartPageTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Create the Start Page widget.
	TSharedRef<SStartPageWindow> Window = SNew(SStartPageWindow);
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
