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
 

#define LOCTEXT_NAMESPACE "PipeEditor"

const FName PipeEditorAppName(TEXT("PipeEditorApp"));
const FName FPipeEditor::AssetDetailsTabId(TEXT("PipeEditor_AssetDetails"));

void FPipeEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (PipeSchematic != nullptr)
	{
		Collector.AddReferencedObject(PipeSchematic);
	}
}

void FPipeEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_PipeEditor", "Pipe Editor"));
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(AssetDetailsTabId, FOnSpawnTab::CreateSP(this, &FPipeEditor::SpawnTab_AssetDetails))
		.SetDisplayName(NSLOCTEXT("PipeEditor", "AssetDetailsTab", "Asset Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FPipeEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(AssetDetailsTabId);
}

void FPipeEditor::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UPipeSchematic& InPipeSchematic)
{
	PipeSchematic = &InPipeSchematic;

	// use the commented block while iterating on the layout since the named layout gets saved
	//FGuid Result;
	//FPlatformMisc::CreateGuid(Result);
	//TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout(*Result.ToString())
	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_Pipe_Layout_v1")
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
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, PipeEditorAppName, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, PipeSchematic);
}

FName FPipeEditor::GetToolkitFName() const
{
	return FName("PipeEditor");
}

FText FPipeEditor::GetBaseToolkitName() const
{
	return NSLOCTEXT("PipeEditor", "AppLabel", "Pipe");
}

FString FPipeEditor::GetWorldCentricTabPrefix() const
{
	return NSLOCTEXT("PipeEditor", "WorldCentricTabPrefix", "Pipe").ToString();
}

FLinearColor FPipeEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.0f, 0.0f, 0.2f, 0.5f );
}

TSharedRef<SDockTab> FPipeEditor::SpawnTab_AssetDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == AssetDetailsTabId);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	AssetDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	AssetDetailsView->SetObject(PipeSchematic);
	AssetDetailsView->OnFinishedChangingProperties().AddSP(this, &FPipeEditor::OnAssetFinishedChangingProperties);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(NSLOCTEXT("PipeEditor", "AssetDetailsTab", "Pipe"))
		[
			AssetDetailsView.ToSharedRef()
		];

	return SpawnedTab;
}

void FPipeEditor::SaveAsset_Execute()
{
	// @todo pre-save validation

	// save it
	FAssetEditorToolkit::SaveAsset_Execute();
}

void FPipeEditor::OnAssetFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	FPipeEditorModule& PipeEditorModule = FModuleManager::GetModuleChecked<FPipeEditorModule>("PipeEditor");
	PipeEditorModule.GetOnAssetPropertiesChanged().Broadcast(PipeSchematic, PropertyChangedEvent);
}


#undef LOCTEXT_NAMESPACE
