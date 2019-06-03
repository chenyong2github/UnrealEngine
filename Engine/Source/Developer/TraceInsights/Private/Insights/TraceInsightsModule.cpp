// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "EditorStyleSet.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/SessionService.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"
//#include "WorkspaceMenuStructure.h"
//#include "WorkspaceMenuStructureModule.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/IoProfilerManager.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/SIoProfilerWindow.h"
#include "Insights/Widgets/SStartPageWindow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Implements the Trace Insights module (TimingProfiler, IoProfiler, etc.).
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

	/** Callback called when the Io Profiler major tab is closed. */
	void OnIoProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Start Page */
	TSharedRef<SDockTab> SpawnStartPageTab(const FSpawnTabArgs& Args);

	/** Timing Profiler */
	TSharedRef<SDockTab> SpawnTimingProfilerTab(const FSpawnTabArgs& Args);

	/** I/O Profiler */
	TSharedRef<SDockTab> SpawnIoProfilerTab(const FSpawnTabArgs& Args);

protected:
	TSharedPtr<Trace::IAnalysisService> TraceAnalysisService;
	TSharedPtr<Trace::ISessionService> TraceSessionService;
	TSharedPtr<Trace::IModuleService> TraceModuleService;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FTraceInsightsModule, TraceInsights);

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
	FIoProfilerManager::Initialize();

	// Register tab spawner for the Start Page.
	auto& StartPageTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterTabSpawner(FInsightsManagerTabs::StartPageTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnStartPageTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "StartPageTabTitle", "Unreal Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "StartPageTooltipText", "Open the start page for Unreal Insights."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "StartPage.Icon.Small"));

	// Register tab spawner for the Timing Insights.
	auto& TimingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterTabSpawner(FInsightsManagerTabs::TimingProfilerTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnTimingProfilerTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTabTitle", "Timing Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTooltipText", "Open the Timing Insights tab."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimingInsights.Icon.Small"));

//#if WITH_EDITOR
//	TimingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
//#else
//	TimingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
//#endif

	// Register tab spawner for the Asset Loading Insights.
	auto& IoProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterTabSpawner(FInsightsManagerTabs::IoProfilerTabId,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnIoProfilerTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "IoProfilerTabTitle", "Asset Loading Insights"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "IoProfilerTooltipText", "Open the Asset Loading Insights tab."))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "AssetLoadingInsights.Icon.Small"));

//#if WITH_EDITOR
//	IoProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
//#else
//	IoProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
//#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::ShutdownModule()
{
	TraceSessionService->StopRecorderServer();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::StartPageTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::TimingProfilerTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::IoProfilerTabId);

	if (FTimingProfilerManager::Get().IsValid())
	{
		// Shutdown the Timing Profiler manager.
		FTimingProfilerManager::Get()->Shutdown();
	}

	if (FIoProfilerManager::Get().IsValid())
	{
		// Shutdown the I/O Profiler manager.
		FIoProfilerManager::Get()->Shutdown();
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
			->AddTab(FInsightsManagerTabs::IoProfilerTabId, ETabState::ClosedTab)
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

TSharedRef<SDockTab> FTraceInsightsModule::SpawnIoProfilerTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle I/O profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FTraceInsightsModule::OnIoProfilerTabBeingClosed));

	// Create the SIoProfilerWindow widget.
	TSharedRef<SIoProfilerWindow> Window = SNew(SIoProfilerWindow);
	FIoProfilerManager::Get()->AssignProfilerWindow(Window);
	DockTab->SetContent(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::OnIoProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
