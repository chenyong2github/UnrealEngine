// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseEditorToolkit.h"
#include "SPoseSearchDatabaseViewport.h"
#include "SPoseSearchDatabaseAssetList.h"
#include "PoseSearchDatabasePreviewScene.h"
#include "PoseSearchDatabaseEditorCommands.h"
#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearchEditor.h"
#include "PoseSearch/PoseSearch.h"
#include "GameFramework/WorldSettings.h"
#include "AdvancedPreviewSceneModule.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "PoseSearchDatabaseEditorToolkit"

namespace UE::PoseSearch
{
	const FName PoseSearchDatabaseEditorAppName = FName(TEXT("PoseSearchDatabaseEditorApp"));

	// Tab identifiers
	struct FDatabaseEditorTabs
	{
		static const FName AssetDetailsID;
		static const FName ViewportID;
		static const FName PreviewSettingsID;
		static const FName AssetTreeViewID;
	};

	const FName FDatabaseEditorTabs::AssetDetailsID(TEXT("PoseSearchDatabaseEditorAssetDetailsTabID"));
	const FName FDatabaseEditorTabs::ViewportID(TEXT("PoseSearchDatabaseEditorViewportTabID"));
	const FName FDatabaseEditorTabs::PreviewSettingsID(TEXT("PoseSearchDatabaseEditorPreviewSettingsTabID"));
	const FName FDatabaseEditorTabs::AssetTreeViewID(TEXT("PoseSearchDatabaseEditorAssetTreeViewTabID"));

	FDatabaseEditorToolkit::FDatabaseEditorToolkit()
	{
	}

	FDatabaseEditorToolkit::~FDatabaseEditorToolkit()
	{
		UPoseSearchDatabase* DatabaseAsset = ViewModel->GetPoseSearchDatabase();
		if (IsValid(DatabaseAsset))
		{
			DatabaseAsset->UnregisterOnAssetChange(AssetTreeWidget.Get());
			DatabaseAsset->UnregisterOnGroupChange(AssetTreeWidget.Get());
		}
	}

	const UPoseSearchDatabase* FDatabaseEditorToolkit::GetPoseSearchDatabase() const
	{
		return ViewModel.IsValid() ? ViewModel->GetPoseSearchDatabase() : nullptr;
	}

	void FDatabaseEditorToolkit::StopPreviewScene()
	{
		ViewModel->RemovePreviewActors();
	}

	void FDatabaseEditorToolkit::ResetPreviewScene()
	{
		ViewModel->ResetPreviewActors();
	}

	void FDatabaseEditorToolkit::BuildSearchIndex()
	{
		ViewModel->BuildSearchIndex();
	}

	void FDatabaseEditorToolkit::InitAssetEditor(
		const EToolkitMode::Type Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		UPoseSearchDatabase* DatabaseAsset)
	{
		// Bind Commands
		BindCommands();

		// Create Preview Scene
		if (!PreviewScene.IsValid())
		{
			PreviewScene = MakeShareable(
				new FDatabasePreviewScene(
					FPreviewScene::ConstructionValues()
					.AllowAudioPlayback(true)
					.ShouldSimulatePhysics(true)
					.ForceUseMovementComponentInNonGameWorld(true),
					StaticCastSharedRef<FDatabaseEditorToolkit>(AsShared())));

			//Temporary fix for missing attached assets - MDW (Copied from FPersonaToolkit::CreatePreviewScene)
			PreviewScene->GetWorld()->GetWorldSettings()->SetIsTemporarilyHiddenInEditor(false);
		}

		// Create view model
		ViewModel = MakeShared<FDatabaseViewModel>();
		ViewModel->Initialize(DatabaseAsset, PreviewScene.ToSharedRef());

		// Create viewport widget
		FDatabaseViewportRequiredArgs ViewportArgs(
			StaticCastSharedRef<FDatabaseEditorToolkit>(AsShared()),
			PreviewScene.ToSharedRef());
		ViewportWidget = SNew(SDatabaseViewport, ViewportArgs);

		AssetTreeWidget = SNew(UE::PoseSearch::SDatabaseAssetTree, ViewModel.ToSharedRef());
		if (IsValid(DatabaseAsset))
		{
			DatabaseAsset->RegisterOnAssetChange(
				UPoseSearchDatabase::FOnDerivedDataRebuild::CreateSP(
					AssetTreeWidget.Get(),
					&UE::PoseSearch::SDatabaseAssetTree::RefreshTreeView, false));
			DatabaseAsset->RegisterOnGroupChange(
				UPoseSearchDatabase::FOnDerivedDataRebuild::CreateSP(
					AssetTreeWidget.Get(),
					&UE::PoseSearch::SDatabaseAssetTree::RefreshTreeView, false));
		}

		// Create Asset Details widget
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs Args;
		Args.bHideSelectionTip = true;
		Args.NotifyHook = this;

		EditingAssetWidget = PropertyModule.CreateDetailView(Args);
		EditingAssetWidget->SetObject(DatabaseAsset);
		EditingAssetWidget->OnFinishedChangingProperties().AddSP(
			this,
			&FDatabaseEditorToolkit::OnFinishedChangingProperties);

		// Define Editor Layout
		const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout =
		FTabManager::NewLayout("Standalone_PoseSearchDatabaseEditor_Layout_v0.04")
			->AddArea
			(
				FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.9f)
					->Split
					(
						FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)->SetSizeCoefficient(0.9f)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.25f)
							->AddTab(FDatabaseEditorTabs::AssetTreeViewID, ETabState::OpenedTab)
							->SetHideTabWell(true)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.4f)
							->AddTab(FDatabaseEditorTabs::ViewportID, ETabState::OpenedTab)
							->SetHideTabWell(true)
						)
						->Split
						(
							FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.3f)
								->AddTab(FDatabaseEditorTabs::AssetDetailsID, ETabState::OpenedTab)
								->AddTab(FDatabaseEditorTabs::PreviewSettingsID, ETabState::OpenedTab)
							)
						)
					)
				)
			);

		const bool bCreateDefaultStandaloneMenuParam = true;
		const bool bCreateDefaultToolbarParam = true;
		const bool bIsToolbarFocusableParam = false;
		FAssetEditorToolkit::InitAssetEditor(
			Mode,
			InitToolkitHost,
			PoseSearchDatabaseEditorAppName,
			StandaloneDefaultLayout,
			bCreateDefaultStandaloneMenuParam,
			bCreateDefaultToolbarParam,
			DatabaseAsset,
			bIsToolbarFocusableParam);

		ExtendToolbar();

		RegenerateMenusAndToolbars();
	}

	void FDatabaseEditorToolkit::BindCommands()
	{
		const FDatabaseEditorCommands& Commands = FDatabaseEditorCommands::Get();

		ToolkitCommands->MapAction(
			Commands.StopPreviewScene,
			FExecuteAction::CreateSP(this, &FDatabaseEditorToolkit::StopPreviewScene),
			EUIActionRepeatMode::RepeatDisabled);

		ToolkitCommands->MapAction(
			Commands.ResetPreviewScene,
			FExecuteAction::CreateSP(this, &FDatabaseEditorToolkit::ResetPreviewScene),
			EUIActionRepeatMode::RepeatDisabled);

		ToolkitCommands->MapAction(
			Commands.BuildSearchIndex,
			FExecuteAction::CreateSP(this, &FDatabaseEditorToolkit::BuildSearchIndex),
			EUIActionRepeatMode::RepeatDisabled);
	}

	void FDatabaseEditorToolkit::ExtendToolbar()
	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

		AddToolbarExtender(ToolbarExtender);

		ToolbarExtender->AddToolBarExtension(
			"Asset",
			EExtensionHook::After,
			GetToolkitCommands(),
			FToolBarExtensionDelegate::CreateSP(this, &FDatabaseEditorToolkit::FillToolbar));
	}

	void FDatabaseEditorToolkit::FillToolbar(FToolBarBuilder& ToolbarBuilder)
	{
		ToolbarBuilder.AddToolBarButton(
			FDatabaseEditorCommands::Get().StopPreviewScene,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Delete"));

		ToolbarBuilder.AddToolBarButton(
			FDatabaseEditorCommands::Get().ResetPreviewScene,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Refresh"));

		ToolbarBuilder.AddToolBarButton(
			FDatabaseEditorCommands::Get().BuildSearchIndex,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon());
	}

	void FDatabaseEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
	{
		WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
			LOCTEXT("WorkspaceMenu_PoseSearchDbEditor", "Pose Search Database Editor"));
		auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

		FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

		InTabManager->RegisterTabSpawner(
			FDatabaseEditorTabs::ViewportID,
			FOnSpawnTab::CreateSP(this, &FDatabaseEditorToolkit::SpawnTab_Viewport))
			.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));

		InTabManager->RegisterTabSpawner(
			FDatabaseEditorTabs::AssetDetailsID,
			FOnSpawnTab::CreateSP(this, &FDatabaseEditorToolkit::SpawnTab_AssetDetails))
			.SetDisplayName(LOCTEXT("AssetDetailsTab", "AssetDetails"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

		InTabManager->RegisterTabSpawner(
			FDatabaseEditorTabs::PreviewSettingsID,
			FOnSpawnTab::CreateSP(this, &FDatabaseEditorToolkit::SpawnTab_PreviewSettings))
			.SetDisplayName(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

		InTabManager->RegisterTabSpawner(
			FDatabaseEditorTabs::AssetTreeViewID,
			FOnSpawnTab::CreateSP(this, &FDatabaseEditorToolkit::SpawnTab_AssetTreeView))
			.SetDisplayName(LOCTEXT("TreeViewTab", "Tree View"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));
	}

	void FDatabaseEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
	{
		FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

		InTabManager->UnregisterTabSpawner(FDatabaseEditorTabs::ViewportID);
		InTabManager->UnregisterTabSpawner(FDatabaseEditorTabs::AssetDetailsID);
		InTabManager->UnregisterTabSpawner(FDatabaseEditorTabs::PreviewSettingsID);
		InTabManager->UnregisterTabSpawner(FDatabaseEditorTabs::AssetTreeViewID);
	}

	FName FDatabaseEditorToolkit::GetToolkitFName() const
	{
		return FName("PoseSearchDatabaseEditor");
	}

	FText FDatabaseEditorToolkit::GetBaseToolkitName() const
	{
		return LOCTEXT("PoseSearchDatabaseEditorAppLabel", "Pose Search Database Editor");
	}

	FText FDatabaseEditorToolkit::GetToolkitName() const
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("AssetName"), FText::FromString(GetPoseSearchDatabase()->GetName()));
		return FText::Format(LOCTEXT("PoseSearchDatabaseEditorToolkitName", "{AssetName}"), Args);
	}

	FLinearColor FDatabaseEditorToolkit::GetWorldCentricTabColorScale() const
	{
		return FLinearColor::White;
	}

	FString FDatabaseEditorToolkit::GetWorldCentricTabPrefix() const
	{
		return TEXT("PoseSearchDatabaseEditor");
	}

	TSharedRef<SDockTab> FDatabaseEditorToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
	{
		check(Args.GetTabId() == FDatabaseEditorTabs::ViewportID);

		TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab).Label(LOCTEXT("ViewportTab_Title", "Viewport"));

		if (ViewportWidget.IsValid())
		{
			SpawnedTab->SetContent(ViewportWidget.ToSharedRef());
		}

		return SpawnedTab;
	}

	TSharedRef<SDockTab> FDatabaseEditorToolkit::SpawnTab_AssetDetails(const FSpawnTabArgs& Args)
	{
		check(Args.GetTabId() == FDatabaseEditorTabs::AssetDetailsID);

		return SNew(SDockTab)
			.Label(LOCTEXT("AssetDetails_Title", "Asset Details"))
			[
				EditingAssetWidget.ToSharedRef()
			];
	}

	TSharedRef<SDockTab> FDatabaseEditorToolkit::SpawnTab_PreviewSettings(const FSpawnTabArgs& Args)
	{
		check(Args.GetTabId() == FDatabaseEditorTabs::PreviewSettingsID);

	FAdvancedPreviewSceneModule& AdvancedPreviewSceneModule = 
		FModuleManager::LoadModuleChecked<FAdvancedPreviewSceneModule>("AdvancedPreviewScene");
	TSharedRef<SWidget> InWidget = 
		AdvancedPreviewSceneModule.CreateAdvancedPreviewSceneSettingsWidget(PreviewScene.ToSharedRef());

		TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
			.Label(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
			[
				InWidget
			];

		return SpawnedTab;
	}

	TSharedRef<SDockTab> FDatabaseEditorToolkit::SpawnTab_AssetTreeView(const FSpawnTabArgs& Args)
	{
		check(Args.GetTabId() == FDatabaseEditorTabs::AssetTreeViewID);

		return SNew(SDockTab)
			.Label(LOCTEXT("AssetTreeView_Title", "Asset Tree"))
			[
				AssetTreeWidget.ToSharedRef()
			];
	}

	void FDatabaseEditorToolkit::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FName PropertyName = 
			(PropertyChangedEvent.Property != nullptr) ? 
			PropertyChangedEvent.Property->GetFName() : NAME_None;
		const FName MemberPropertyName = 
			(PropertyChangedEvent.MemberProperty != nullptr) ? 
			PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

		UE_LOG(
			LogPoseSearchEditor,
			Log,
			TEXT("FPoseSearchDatabaseEditorToolkit::OnFinishedChangingProperties MemberPropertyName: %s PropertyName: %s"),
			*MemberPropertyName.ToString(),
			*PropertyName.ToString());
	}
}

#undef LOCTEXT_NAMESPACE
