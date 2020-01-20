// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceDataFilteringModule.h"

#include "Modules/ModuleInterface.h"
#include "Features/IModularFeatures.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Templates/SharedPointer.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"
#include "EditorStyleSet.h"

#include "TraceServices/ModuleService.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "Insights/IUnrealInsightsModule.h"

#include "STraceDataFilterWidget.h"

IMPLEMENT_MODULE(FTraceFilteringModule, TraceDataFiltering);

FName FTraceFilteringModule::InsightsFilterTabName = TEXT("TraceDataFiltering");

FString FTraceFilteringModule::TraceFiltersIni;

void FTraceFilteringModule::StartupModule()
{
	FEventFilterStyle::Initialize();

	FConfigCacheIni::LoadGlobalIniFile(TraceFiltersIni, TEXT("TraceDataFilters"));

	FEventFilterService::Get();
	
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

#if WITH_EDITOR
	FilterTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory());
#else
	FilterTabSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
#endif	
}

void FTraceFilteringModule::ShutdownModule()
{
	FEventFilterStyle::Shutdown();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FTraceFilteringModule::InsightsFilterTabName);
}