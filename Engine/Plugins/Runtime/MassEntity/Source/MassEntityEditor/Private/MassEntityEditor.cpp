// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityEditor.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Modules/ModuleManager.h"
#include "EditorStyleSet.h"
#include "PropertyEditorModule.h"
#include "MassEntityEditorModule.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "MassSchematic.h"
#include "IDetailsView.h"
#include "Widgets/Docking/SDockTab.h"
 

#define LOCTEXT_NAMESPACE "MassEntityEditor"

const FName MassEntityEditorAppName(TEXT("MassEntityEditorApp"));
const FName FMassEntityEditor::AssetDetailsTabId(TEXT("MassEntityEditor_AssetDetails"));

void FMassEntityEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (MassSchematic != nullptr)
	{
		Collector.AddReferencedObject(MassSchematic);
	}
}

void FMassEntityEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MassEntityEditor", "Mass Editor"));
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(AssetDetailsTabId, FOnSpawnTab::CreateSP(this, &FMassEntityEditor::SpawnTab_AssetDetails))
		.SetDisplayName(NSLOCTEXT("MassEntityEditor", "AssetDetailsTab", "Asset Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FMassEntityEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(AssetDetailsTabId);
}

void FMassEntityEditor::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UMassSchematic& InMassSchematic)
{
	MassSchematic = &InMassSchematic;

	// use the commented block while iterating on the layout since the named layout gets saved
	//FGuid Result;
	//FPlatformMisc::CreateGuid(Result);
	//TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout(*Result.ToString())
	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_Mass_Layout_v1")
	->AddArea
	(
		FTabManager::NewPrimaryArea() ->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.2f)
			->AddTab(AssetDetailsTabId, ETabState::OpenedTab)
			->SetForegroundTab(AssetDetailsTabId)
		)
	);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, MassEntityEditorAppName, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, MassSchematic);
}

FName FMassEntityEditor::GetToolkitFName() const
{
	return FName("MassEntityEditor");
}

FText FMassEntityEditor::GetBaseToolkitName() const
{
	return NSLOCTEXT("MassEntityEditor", "AppLabel", "Mass");
}

FString FMassEntityEditor::GetWorldCentricTabPrefix() const
{
	return NSLOCTEXT("MassEntityEditor", "WorldCentricTabPrefix", "Mass").ToString();
}

FLinearColor FMassEntityEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.0f, 0.0f, 0.2f, 0.5f );
}

TSharedRef<SDockTab> FMassEntityEditor::SpawnTab_AssetDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == AssetDetailsTabId);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	AssetDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	AssetDetailsView->SetObject(MassSchematic);
	AssetDetailsView->OnFinishedChangingProperties().AddSP(this, &FMassEntityEditor::OnAssetFinishedChangingProperties);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(NSLOCTEXT("MassEntityEditor", "AssetDetailsTabLabel", "Mass"))
		[
			AssetDetailsView.ToSharedRef()
		];

	return SpawnedTab;
}

void FMassEntityEditor::SaveAsset_Execute()
{
	// @todo pre-save validation

	// save it
	FAssetEditorToolkit::SaveAsset_Execute();
}

void FMassEntityEditor::OnAssetFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	FMassEntityEditorModule& MassEntityEditorModule = FModuleManager::GetModuleChecked<FMassEntityEditorModule>("MassEntityEditor");
	MassEntityEditorModule.GetOnAssetPropertiesChanged().Broadcast(MassSchematic, PropertyChangedEvent);
}


#undef LOCTEXT_NAMESPACE
