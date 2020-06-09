// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/BaseAssetToolkit.h"
#include "Framework/Docking/TabManager.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "EditorViewportTabContent.h"
#include "SAssetEditorViewport.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "PreviewScene.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Modules/ModuleManager.h"
#include "Tools/UAssetEditor.h"


#define LOCTEXT_NAMESPACE "BaseAssetToolkit"

const FName FBaseAssetToolkit::ViewportTabID(TEXT("BaseAssetToolkit_Viewport"));
const FName FBaseAssetToolkit::DetailsTabID(TEXT("BaseAssetToolkit_Details"));

FBaseAssetToolkit::FBaseAssetToolkit(UAssetEditor* InOwningAssetEditor)
	: FAssetEditorToolkit()
{
	OwningAssetEditor = InOwningAssetEditor;

	FString LayoutString = TEXT("Standalone_Test_Layout_") + LayoutAppendix;
	StandaloneDefaultLayout = FTabManager::NewLayout(FName(LayoutString))
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
				->SetHideTabWell(true)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.7f)
					->AddTab(ViewportTabID, ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab(DetailsTabID, ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
			)
		);
}

FBaseAssetToolkit::~FBaseAssetToolkit()
{
	OwningAssetEditor->OnToolkitClosed();
}

void FBaseAssetToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	if(!AssetEditorTabsCategory.IsValid())
	{
		// Use the first child category of the local workspace root if there is one, otherwise use the root itself
		const auto& LocalCategories = InTabManager->GetLocalWorkspaceMenuRoot()->GetChildItems();
		AssetEditorTabsCategory = LocalCategories.Num() > 0 ? LocalCategories[0] : InTabManager->GetLocalWorkspaceMenuRoot();
	}

	InTabManager->RegisterTabSpawner(ViewportTabID, FOnSpawnTab::CreateSP(this, &FBaseAssetToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(DetailsTabID, FOnSpawnTab::CreateSP(this, &FBaseAssetToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("Details", "Details"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

}

const TSharedRef<FTabManager::FLayout> FBaseAssetToolkit::GetDefaultLayout() const
{
	return StandaloneDefaultLayout.ToSharedRef();
}

TSharedRef<SDockTab> FBaseAssetToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	TSharedRef< SDockTab > DockableTab =
		SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Viewports"));

	const FString LayoutId = FString("BaseAssetViewport");
	ViewportTabContent->Initialize(ViewportDelegate, DockableTab, LayoutId);
	return DockableTab;
}

TSharedRef<SDockTab>  FBaseAssetToolkit::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> DetailsTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("BaseDetailsTitle", "Details"))
		[
			DetailsView.ToSharedRef()
		];

	return DetailsTab.ToSharedRef();
}

void FBaseAssetToolkit::RegisterToolbar()
{
}

TFunction<TSharedRef<SEditorViewport>(void)> FBaseAssetToolkit::GetViewportDelegate()
{
	// Set up functor for viewport tab
	TFunction<TSharedRef<SEditorViewport>(void)> TempViewportDelegate = [=]()
	{
		return SNew(SAssetEditorViewport)
			.EditorViewportClient(ViewportClient);
	};

	return TempViewportDelegate;
}

TSharedPtr<FEditorViewportClient> FBaseAssetToolkit::CreateEditorViewportClient() const
{
	FPreviewScene* PreviewScene = new FPreviewScene(FPreviewScene::ConstructionValues());
	return MakeShared<FEditorViewportClient>(nullptr, PreviewScene);
}

void FBaseAssetToolkit::CreateWidgets()
{
	RegisterToolbar();
	ViewportClient = CreateEditorViewportClient();
	ViewportDelegate = GetViewportDelegate();
	ViewportTabContent = MakeShareable(new FEditorViewportTabContent());
	LayoutExtender = MakeShared<FLayoutExtender>();

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	const FDetailsViewArgs DetailsViewArgs(false, false, true, FDetailsViewArgs::ObjectsUseNameArea, true);
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
}

void FBaseAssetToolkit::SetEditingObject(class UObject* InObject)
{
	DetailsView->SetObject(InObject);
}

#undef LOCTEXT_NAMESPACE