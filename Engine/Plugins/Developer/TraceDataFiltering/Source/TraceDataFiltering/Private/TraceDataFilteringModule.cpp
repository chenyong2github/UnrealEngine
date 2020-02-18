// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceDataFilteringModule.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Templates/SharedPointer.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"
#include "EditorStyleSet.h"
#include "Widgets/Docking/SDockTab.h"
#include "Misc/ConfigCacheIni.h"

#include "TraceServices/ModuleService.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "Insights/IUnrealInsightsModule.h"

#include "STraceDataFilterWidget.h"
#include "EventFilterStyle.h"

IMPLEMENT_MODULE(FTraceFilteringModule, TraceDataFiltering);

FName FTraceFilteringModule::InsightsFilterTabName = TEXT("TraceDataFiltering");

FString FTraceFilteringModule::TraceFiltersIni;

void FTraceFilteringModule::StartupModule()
{
	FEventFilterStyle::Initialize();

	FConfigCacheIni::LoadGlobalIniFile(TraceFiltersIni, TEXT("TraceDataFilters"));

#if WITH_EDITOR
	const FSlateIcon TabIcon(FEventFilterStyle::GetStyleSetName(), "EventFilter.TabIcon");
	FTabSpawnerEntry& FilterTabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FTraceFilteringModule::InsightsFilterTabName,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args)
		{
			const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
				.TabRole(ETabRole::NomadTab);

			TSharedRef<STraceDataFilterWidget> Window = SNew(STraceDataFilterWidget);
			DockTab->SetContent(Window);

			return DockTab;
	}))
	.SetDisplayName(NSLOCTEXT("FTraceInsightsModule", "FilteringTabTitle", "Trace Data Filtering"))
	.SetIcon(TabIcon)
	.SetTooltipText(NSLOCTEXT("FTraceInsightsModule", "FilteringTabTooltip", "Opens the Trace Data Filtering tab, allows for setting Trace Channel states"));

	FilterTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory());
#else
	IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

	FOnRegisterMajorTabExtensions& TimingProfilerLayoutExtension = UnrealInsightsModule.OnRegisterMajorTabExtension(FInsightsManagerTabs::TimingProfilerTabId);
	TimingProfilerLayoutExtension.AddRaw(this, &FTraceFilteringModule::RegisterTimingProfilerLayoutExtensions);
#endif
}

void FTraceFilteringModule::ShutdownModule()
{
	FEventFilterStyle::Shutdown();
#if WITH_EDITOR
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FTraceFilteringModule::InsightsFilterTabName);
#else
	IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	FOnRegisterMajorTabExtensions& TimingProfilerLayoutExtension = UnrealInsightsModule.OnRegisterMajorTabExtension(FInsightsManagerTabs::TimingProfilerTabId);
	TimingProfilerLayoutExtension.RemoveAll(this);
#endif
}

void FTraceFilteringModule::RegisterTimingProfilerLayoutExtensions(FInsightsMajorTabExtender& InOutExtender)
{
	TSharedRef<FWorkspaceItem> Category = InOutExtender.GetTabManager()->AddLocalWorkspaceMenuCategory(NSLOCTEXT("FTraceInsightsModule", "FilteringCategoryLabel", "Filtering"));

	FInsightsMinorTabConfig& MinorTabConfig = InOutExtender.AddMinorTabConfig();
	MinorTabConfig.TabId = FTraceFilteringModule::InsightsFilterTabName;
	MinorTabConfig.TabLabel = NSLOCTEXT("FTraceInsightsModule", "FilteringTabTitle", "Trace Data Filtering");
	MinorTabConfig.TabTooltip = NSLOCTEXT("FTraceInsightsModule", "FilteringTabTooltip", "Opens the Trace Data Filtering tab, allows for setting Trace Channel states");
	MinorTabConfig.TabIcon = FSlateIcon(FEventFilterStyle::GetStyleSetName(), "EventFilter.TabIcon");		
	MinorTabConfig.WorkspaceGroup = Category;
	MinorTabConfig.OnSpawnTab = FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args)
	{
		const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
			.TabRole(ETabRole::NomadTab);

		TSharedRef<STraceDataFilterWidget> Window = SNew(STraceDataFilterWidget);
		DockTab->SetContent(Window);

		return DockTab;
	});

	InOutExtender.GetLayoutExtender().ExtendLayout(FTimingProfilerTabs::TimersID, ELayoutExtensionPosition::Before, FTabManager::FTab(FTraceFilteringModule::InsightsFilterTabName, ETabState::ClosedTab));
}