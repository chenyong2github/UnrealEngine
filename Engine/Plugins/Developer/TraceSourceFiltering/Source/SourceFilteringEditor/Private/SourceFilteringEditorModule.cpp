// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceFilteringEditorModule.h"
#include "Modules/ModuleManager.h"

#include "TraceServices/ITraceServicesModule.h"
#include "HAL/IConsoleManager.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Misc/ConfigCacheIni.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"
#include "Features/IModularFeatures.h"

#include "STraceSourceFilteringWidget.h"
#include "SourceFilterStyle.h"

#include "IGameplayInsightsModule.h"

IMPLEMENT_MODULE(FSourceFilteringEditorModule, SourceFilteringEditor);

FString FSourceFilteringEditorModule::SourceFiltersIni;
FName InsightsSourceFilteringTabName(TEXT("InsightsSourceFiltering"));

void FSourceFilteringEditorModule::StartupModule()
{
	FSourceFilterStyle::Initialize();

	// Populate static ini path
	FConfigCacheIni::LoadGlobalIniFile(SourceFiltersIni, TEXT("TraceSourceFilters"));
	
	IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");	
	FOnRegisterMajorTabExtensions& TimingProfilerLayoutExtension = UnrealInsightsModule.OnRegisterMajorTabExtension(FInsightsManagerTabs::TimingProfilerTabId);
	TimingProfilerLayoutExtension.AddRaw(this, &FSourceFilteringEditorModule::RegisterLayoutExtensions);
}

void FSourceFilteringEditorModule::ShutdownModule()
{
	FSourceFilterStyle::Shutdown();
}

void FSourceFilteringEditorModule::RegisterLayoutExtensions(FInsightsMajorTabExtender& InOutExtender)
{
#if WITH_EDITOR
	FTabId ExtendedTabId(GameplayInsightsTabs::DocumentTab);
#else
	FTabId ExtendedTabId(FTimingProfilerTabs::TimersID);
#endif

	InOutExtender.GetLayoutExtender().ExtendLayout(ExtendedTabId, ELayoutExtensionPosition::Before, FTabManager::FTab(InsightsSourceFilteringTabName, ETabState::ClosedTab));

	TSharedRef<FWorkspaceItem> Category = InOutExtender.GetTabManager()->AddLocalWorkspaceMenuCategory(NSLOCTEXT("FInsightsSourceFilteringModule", "SourceFilteringGroupName", "Filtering"));

	FInsightsMinorTabConfig& MinorTabConfig = InOutExtender.AddMinorTabConfig();
	MinorTabConfig.TabId = InsightsSourceFilteringTabName;
	MinorTabConfig.TabLabel = NSLOCTEXT("SourceFilteringEditorModule", "SourceFilteringTab", "Trace Source Filtering");
	MinorTabConfig.TabTooltip = NSLOCTEXT("SourceFilteringEditorModule", "SourceFilteringTabTooltip", "Opens the Trace Source Filtering tab, allows for filtering UWorld and AActor instances to not output Trace data");
	MinorTabConfig.TabIcon = FSlateIcon(FSourceFilterStyle::GetStyleSetName(), "SourceFilter.TabIcon");
	MinorTabConfig.WorkspaceGroup = Category;
	MinorTabConfig.OnSpawnTab = FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args)
	{
		const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
			.TabRole(ETabRole::PanelTab);

		TSharedRef<STraceSourceFilteringWidget> Window = SNew(STraceSourceFilteringWidget);
		DockTab->SetContent(Window);

		return DockTab;
	});
}
