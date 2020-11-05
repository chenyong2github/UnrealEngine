// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters.h"

#include "Widgets/Docking/SDockTab.h"

#include "Insights/Widgets/SFilterConfigurator.h"
#include "Insights/ViewModels/FilterConfigurator.h"

#define LOCTEXT_NAMESPACE "SFilterService"

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FFilterService> FFilterService::Instance;

FName const FFilterService::FilterConfiguratorTabId(TEXT("FilterConfigurator"));

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterService::FFilterService()
{
	RegisterTabSpawner();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterService::~FFilterService()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> FFilterService::CreateFilterConfiguratorWidget(TSharedPtr<FFilterConfigurator> FilterConfiguratorViewModel)
{
	SAssignNew(PendingWidget, SFilterConfigurator, FilterConfiguratorViewModel);

	if (FGlobalTabmanager::Get()->HasTabSpawner(FilterConfiguratorTabId))
	{
		FGlobalTabmanager::Get()->TryInvokeTab(FilterConfiguratorTabId);
	}

	return PendingWidget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FFilterService::SpawnTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	DockTab->SetContent(PendingWidget.ToSharedRef());
	PendingWidget->SetParentTab(DockTab);

	PendingWidget = nullptr;
	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFilterService::RegisterTabSpawner()
{
	FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FilterConfiguratorTabId,
		FOnSpawnTab::CreateRaw(this, &FFilterService::SpawnTab))
		.SetDisplayName(LOCTEXT("FilterConfiguratorTabTitle", "Filter Configurator"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE