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
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/InsightsManager.h"
#include "Insights/IoProfilerManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/SIoProfilerWindow.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

static const FName StartPageTabName("StartPage");
static const FName TimingProfilerTabName("TimingProfiler");
static const FName IoProfilerTabName("IoProfiler");
//static const FName MemoryProfilerTabName("MemoryProfiler");

////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// Implements the Trace Insights module (TimingProfiler, IoProfiler, etc.).
///
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
	/// Callback called when a major tab is closed.
	void OnTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/// Callback called when the Timing Profiler major tab is closed.
	void OnTimingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/// Callback called when the Io Profiler major tab is closed.
	void OnIoProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/// Start Page
	TSharedRef<SDockTab> SpawnStartPageTab(const FSpawnTabArgs& Args);

	/// Timing Profiler
	TSharedRef<SDockTab> SpawnTimingProfilerTab(const FSpawnTabArgs& Args);

	/// I/O Profiler
	TSharedRef<SDockTab> SpawnIoProfilerTab(const FSpawnTabArgs& Args);

protected:
	TSharedPtr<Trace::IAnalysisService> TraceAnalysisService;
	TSharedPtr<Trace::ISessionService> TraceSessionService;
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
	TraceSessionService->StartRecorderServer();

	FInsightsManager::Initialize(TraceAnalysisService.ToSharedRef(), TraceSessionService.ToSharedRef());
	FTimingProfilerManager::Initialize();
	FIoProfilerManager::Initialize();

	// Register tab spawner for the Start Page.
	auto& StartPageTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterTabSpawner(StartPageTabName,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnStartPageTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "StartPageTabTitle", "Start Page"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "StartPageTooltipText", "Open the Unreal Insights start page."));
		//.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Tab")); //im:TODO

	// Register tab spawner for the Timing Profiler.
	auto& TimingProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterTabSpawner(TimingProfilerTabName,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnTimingProfilerTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTabTitle", "Timing Profiler"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "TimingProfilerTooltipText", "Open the Timing Profiler tab."));
		//.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Tab")); //im:TODO

//#if WITH_EDITOR
//	TimingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
//#else
//	TimingProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
//#endif

	// Register tab spawner for the I/O Profiler.
	auto& IoProfilerTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterTabSpawner(IoProfilerTabName,
		FOnSpawnTab::CreateRaw(this, &FTraceInsightsModule::SpawnIoProfilerTab))
		.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "IoProfilerTabTitle", "I/O Profiler"))
		.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "IoProfilerTooltipText", "Open the Timing Profiler tab."));
		//.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.Tab")); //im:TODO

//#if WITH_EDITOR
//	IoProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
//#else
//	IoProfilerTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
//#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StartPageTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TimingProfilerTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(IoProfilerTabName);

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
			->AddTab(StartPageTabName, ETabState::OpenedTab)
			->AddTab(TimingProfilerTabName, ETabState::OpenedTab)
			->AddTab(IoProfilerTabName, ETabState::OpenedTab)
			//->AddTab(FName("MemoryProfiler"), ETabState::OpenedTab)
			->SetForegroundTab(FTabId(TimingProfilerTabName))
		)
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::OnLayoutRestored(TSharedPtr<FTabManager> TabManager)
{
	// Set the Start Page as the main tab.
	//TSharedPtr<SDockTab> StartPageDockTab = TabManager->FindExistingLiveTab(FTabId(FName("StartPage")));
	//if (StartPageDockTab.IsValid())
	//{
	//	TabManager->SetMainTab(StaticCastSharedRef<SDockTab>(StartPageDockTab->AsShared()));
	//}

	// Activate the Timing Profiler tab.
	//TSharedPtr<SDockTab> TimingProfilerDockTab = TabManager->FindExistingLiveTab(FTabId(TimingProfilerTabName));
	//if (TimingProfilerDockTab.IsValid())
	//{
	//	TabManager->SetActiveTab(TimingProfilerDockTab);
	//}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTraceInsightsModule::SpawnStartPageTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		//.Icon(FEditorStyle::GetBrush("Launcher.TabIcon")) //TODO
		.TabRole(ETabRole::NomadTab);

	// Create the Start Page widget.
	TSharedRef<STextBlock> ProfilerWindow = SNew(STextBlock)
		.Text(NSLOCTEXT("FTraceInsightsModule", "StartPageContent", "TODO"))
		.ColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
	DockTab->SetContent(ProfilerWindow);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTraceInsightsModule::SpawnTimingProfilerTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		//.Icon(FEditorStyle::GetBrush("Profiler.Tab")) //TODO
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle Timing profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FTraceInsightsModule::OnTimingProfilerTabBeingClosed));

	// Create the STimingProfilerWindow widget.
	TSharedRef<STimingProfilerWindow> ProfilerWindow = SNew(STimingProfilerWindow);
	FTimingProfilerManager::Get()->AssignProfilerWindow(ProfilerWindow);
	DockTab->SetContent(ProfilerWindow);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::OnTimingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());

	// Shutdown the Timing Profiler manager.
	FTimingProfilerManager::Get()->Shutdown();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTraceInsightsModule::SpawnIoProfilerTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		//.Icon(FEditorStyle::GetBrush("Profiler.Tab")) //TODO
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle I/O profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FTraceInsightsModule::OnIoProfilerTabBeingClosed));

	// Create the SIoProfilerWindow widget.
	TSharedRef<SIoProfilerWindow> ProfilerWindow = SNew(SIoProfilerWindow);
	FIoProfilerManager::Get()->AssignProfilerWindow(ProfilerWindow);
	DockTab->SetContent(ProfilerWindow);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::OnIoProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());

	// Shutdown the I/O manager.
	FIoProfilerManager::Get()->Shutdown();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
