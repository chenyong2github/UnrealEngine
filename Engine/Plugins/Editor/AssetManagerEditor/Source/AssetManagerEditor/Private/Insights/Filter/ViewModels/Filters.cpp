// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters.h"

#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Docking/SDockTab.h"

// Insights
#include "Insights/Common/InsightsStyle.h"
#include "Insights/Filter/ViewModels/FilterConfigurator.h"
#include "Insights/Filter/Widgets/SAdvancedFilter.h"

#define LOCTEXT_NAMESPACE "SFilterService"

namespace UE
{
namespace Insights
{

INSIGHTS_IMPLEMENT_RTTI(FFilter)
INSIGHTS_IMPLEMENT_RTTI(FFilterWithSuggestions)

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FFilterService> FFilterService::Instance;

FName const FFilterService::FilterConfiguratorTabId(TEXT("AssetManager/FilterConfigurator"));

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFilterService::Initialize()
{
	Instance = MakeShared<FFilterService>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFilterService::Shutdown()
{
	Instance = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterService::FFilterService()
{
	RegisterTabSpawner();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterService::~FFilterService()
{
	UnregisterTabSpawner();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SWidget> FFilterService::CreateFilterConfiguratorWidget(TSharedPtr<FFilterConfigurator> FilterConfiguratorViewModel)
{
	SAssignNew(PendingWidget, SAdvancedFilter, FilterConfiguratorViewModel);

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

	const TSharedPtr<SWindow>& OwnerWindow = Args.GetOwnerWindow();
	if (OwnerWindow.IsValid())
	{
		const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);
		OwnerWindow->Resize(FVector2D(600 * DPIScaleFactor, 400 * DPIScaleFactor));
	}

	check(PendingWidget.IsValid());
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
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.ClassicFilterConfig"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFilterService::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FilterConfiguratorTabId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
} // namespace UE

#undef LOCTEXT_NAMESPACE