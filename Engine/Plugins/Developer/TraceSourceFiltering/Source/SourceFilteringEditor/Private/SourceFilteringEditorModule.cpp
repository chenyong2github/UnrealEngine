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

#include "AssetRegistryModule.h"
#include "EmptySourceFilter.h"
#include "SourceFilterCollection.h"
#include "TraceSourceFiltering.h"

#include "STraceSourceFilteringWidget.h"
#include "SourceFilterStyle.h"

#include "IGameplayInsightsModule.h"

#include "Engine/Blueprint.h"
#include "Editor.h"

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
	
#if WITH_EDITOR
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().OnInMemoryAssetDeleted().AddRaw(this, &FSourceFilteringEditorModule::HandleAssetDeleted);
	FEditorDelegates::OnAssetsPreDelete.AddRaw(this, &FSourceFilteringEditorModule::OnAssetsPendingDelete);
#endif // WITH_EDITOR
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

#if WITH_EDITOR
void FSourceFilteringEditorModule::HandleAssetDeleted(UObject* DeletedObject)
{
	// Remove all pending filter deletions for which the class is actually deleted
	PendingDeletions.RemoveAll([&DeletedObject](FPendingFilterDeletion& PendingDelete)
	{
		return PendingDelete.ToDeleteFilterClassObject == DeletedObject;
	});
}

void FSourceFilteringEditorModule::OnAssetsPendingDelete(TArray<UObject*> const& ObjectsForDelete)
{
	USourceFilterCollection* FilterCollection = FTraceSourceFiltering::Get().GetFilterCollection();

	if (FilterCollection)
	{
		TArray<UDataSourceFilter*> Filters;
		FilterCollection->GetFlatFilters(Filters);
		for (UDataSourceFilter* Filter : Filters) 
		{
			// Check whether or not the to-be deleted objects contains this filter its blueprint class
			UObject* const* DeletedClassObjectPtr = ObjectsForDelete.FindByPredicate([Filter](UObject* Obj) -> bool
			{
				if (UBlueprint* Blueprint = Cast<UBlueprint>(Obj))
				{
					return Blueprint->GeneratedClass.Get() == Filter->GetClass();
				}

				return false;
			});
			
			// If so enqueue a pending deletion, used to keep track of cancelled deletions
			if (DeletedClassObjectPtr)
			{
				UEmptySourceFilter* EmptyFilter = NewObject<UEmptySourceFilter>(FilterCollection);
				EmptyFilter->MissingClassName = Filter->GetClass()->GetName();

				PendingDeletions.Add({ Filter, EmptyFilter, *DeletedClassObjectPtr });
				// Replace the to-be removed filter with an empty filter instance
				FilterCollection->ReplaceFilter(Filter, EmptyFilter);
			}
		}

		if (PendingDeletions.Num())
		{
			// In case filters are due to be removed, enqueue a callback during the next frame's tick to handle cancelled deletes
			GEditor->GetTimerManager()->SetTimerForNextTick([FilterCollection, this]()
			{
				for (FPendingFilterDeletion& PendingDeletion : PendingDeletions)
				{
					FilterCollection->ReplaceFilter(PendingDeletion.ReplacementFilter, PendingDeletion.FilterWithDeletedClass);
				}
				PendingDeletions.Empty();
			});
		}
	}


}
#endif // WITH_EDITOR