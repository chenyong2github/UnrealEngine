// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseEditorToolkit.h"
#include "SPoseSearchDatabaseViewport.h"
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

const FName PoseSearchDatabaseEditorAppName = FName(TEXT("PoseSearchDatabaseEditorApp"));

// Tab identifiers
struct FPoseSearchDatabaseEditorTabs
{
	static const FName AssetDetailsID;
	static const FName ViewportID;
	static const FName PreviewSettingsID;
};

const FName FPoseSearchDatabaseEditorTabs::AssetDetailsID(TEXT("PoseSearchDatabaseEditorAssetDetailsTabID"));
const FName FPoseSearchDatabaseEditorTabs::ViewportID(TEXT("PoseSearchDatabaseEditorViewportTabID"));
const FName FPoseSearchDatabaseEditorTabs::PreviewSettingsID(TEXT("PoseSearchDatabaseEditorPreviewSettingsTabID"));

FPoseSearchDatabaseEditorToolkit::FPoseSearchDatabaseEditorToolkit()
{
}

FPoseSearchDatabaseEditorToolkit::~FPoseSearchDatabaseEditorToolkit()
{
}

const UPoseSearchDatabase* FPoseSearchDatabaseEditorToolkit::GetPoseSearchDatabase() const
{
	return ViewModel.IsValid() ? ViewModel->GetPoseSearchDatabase() : nullptr;
}

void FPoseSearchDatabaseEditorToolkit::StopPreviewScene()
{
	ViewModel->RemovePreviewActors();
}

void FPoseSearchDatabaseEditorToolkit::ResetPreviewScene()
{
	ViewModel->ResetPreviewActors();
}

void FPoseSearchDatabaseEditorToolkit::BuildSearchIndex()
{
	ViewModel->BuildSearchIndex();
}

void FPoseSearchDatabaseEditorToolkit::InitAssetEditor(
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
			new FPoseSearchDatabasePreviewScene(
				FPreviewScene::ConstructionValues()
				.AllowAudioPlayback(true)
				.ShouldSimulatePhysics(true)
				.ForceUseMovementComponentInNonGameWorld(true),
				StaticCastSharedRef<FPoseSearchDatabaseEditorToolkit>(AsShared())));

		//Temporary fix for missing attached assets - MDW (Copied from FPersonaToolkit::CreatePreviewScene)
		PreviewScene->GetWorld()->GetWorldSettings()->SetIsTemporarilyHiddenInEditor(false);
	}

	// Create view model
	ViewModel = MakeShared<FPoseSearchDatabaseViewModel>();
	ViewModel->Initialize(DatabaseAsset, PreviewScene.ToSharedRef());

	// Create viewport widget
	FPoseSearchDatabaseViewportRequiredArgs ViewportArgs(
		StaticCastSharedRef<FPoseSearchDatabaseEditorToolkit>(AsShared()), 
		PreviewScene.ToSharedRef());
	ViewportWidget = SNew(SPoseSearchDatabaseViewport, ViewportArgs);

	// Create Asset Details widget
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NotifyHook = this;

	EditingAssetWidget = PropertyModule.CreateDetailView(Args);
	EditingAssetWidget->SetObject(DatabaseAsset);
	EditingAssetWidget->OnFinishedChangingProperties().AddSP(
		this, 
		&FPoseSearchDatabaseEditorToolkit::OnFinishedChangingProperties);

	// Define Editor Layout
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = 
		FTabManager::NewLayout("Standalone_PoseSearchDatabaseEditor_Layout_v0.02")
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
						->SetSizeCoefficient(0.65f)
						->AddTab(FPoseSearchDatabaseEditorTabs::ViewportID, ETabState::OpenedTab)
						->SetHideTabWell(true)
					)
					->Split
					(
						FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.3f)
							->AddTab(FPoseSearchDatabaseEditorTabs::AssetDetailsID, ETabState::OpenedTab)
							->AddTab(FPoseSearchDatabaseEditorTabs::PreviewSettingsID, ETabState::OpenedTab)
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

void FPoseSearchDatabaseEditorToolkit::BindCommands()
{
	const FPoseSearchDatabaseEditorCommands& Commands = FPoseSearchDatabaseEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.StopPreviewScene,
		FExecuteAction::CreateSP(this, &FPoseSearchDatabaseEditorToolkit::StopPreviewScene),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.ResetPreviewScene,
		FExecuteAction::CreateSP(this, &FPoseSearchDatabaseEditorToolkit::ResetPreviewScene),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.BuildSearchIndex,
		FExecuteAction::CreateSP(this, &FPoseSearchDatabaseEditorToolkit::BuildSearchIndex),
		EUIActionRepeatMode::RepeatDisabled);
}

void FPoseSearchDatabaseEditorToolkit::ExtendToolbar()
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FPoseSearchDatabaseEditorToolkit::FillToolbar));
}

void FPoseSearchDatabaseEditorToolkit::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.AddToolBarButton(
		FPoseSearchDatabaseEditorCommands::Get().StopPreviewScene,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Delete"));

	ToolbarBuilder.AddToolBarButton(
		FPoseSearchDatabaseEditorCommands::Get().ResetPreviewScene,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Refresh"));

	ToolbarBuilder.AddToolBarButton(
		FPoseSearchDatabaseEditorCommands::Get().BuildSearchIndex,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon());
}

void FPoseSearchDatabaseEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
		LOCTEXT("WorkspaceMenu_PoseSearchDbEditor", "Pose Search Database Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(
		FPoseSearchDatabaseEditorTabs::ViewportID, 
		FOnSpawnTab::CreateSP(this, &FPoseSearchDatabaseEditorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(
		FPoseSearchDatabaseEditorTabs::AssetDetailsID, 
		FOnSpawnTab::CreateSP(this, &FPoseSearchDatabaseEditorToolkit::SpawnTab_AssetDetails))
		.SetDisplayName(LOCTEXT("AssetDetailsTab", "AssetDetails"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(
		FPoseSearchDatabaseEditorTabs::PreviewSettingsID, 
		FOnSpawnTab::CreateSP(this, &FPoseSearchDatabaseEditorToolkit::SpawnTab_PreviewSettings))
		.SetDisplayName(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FPoseSearchDatabaseEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	// InTabManager->UnregisterTabSpawner(FPoseSearchDatabaseEditorTabs::ViewportID);
	InTabManager->UnregisterTabSpawner(FPoseSearchDatabaseEditorTabs::AssetDetailsID);
	// InTabManager->UnregisterTabSpawner(FPoseSearchDatabaseEditorTabs::PreviewSettingsID);
}

FName FPoseSearchDatabaseEditorToolkit::GetToolkitFName() const
{
	return FName("PoseSearchDatabaseEditor");
}

FText FPoseSearchDatabaseEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("PoseSearchDatabaseEditorAppLabel", "Pose Search Database Editor");
}

FText FPoseSearchDatabaseEditorToolkit::GetToolkitName() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), FText::FromString(GetPoseSearchDatabase()->GetName()));
	return FText::Format(LOCTEXT("PoseSearchDatabaseEditorToolkitName", "{AssetName}"), Args);
}

FLinearColor FPoseSearchDatabaseEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

FString FPoseSearchDatabaseEditorToolkit::GetWorldCentricTabPrefix() const
{
	return TEXT("PoseSearchDatabaseEditor");
}

TSharedRef<SDockTab> FPoseSearchDatabaseEditorToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FPoseSearchDatabaseEditorTabs::ViewportID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab).Label(LOCTEXT("ViewportTab_Title", "Viewport"));

	if (ViewportWidget.IsValid())
	{
		SpawnedTab->SetContent(ViewportWidget.ToSharedRef());
	}

	return SpawnedTab;
}

TSharedRef<SDockTab> FPoseSearchDatabaseEditorToolkit::SpawnTab_AssetDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FPoseSearchDatabaseEditorTabs::AssetDetailsID);

	return SNew(SDockTab)
		.Label(LOCTEXT("AssetDetails_Title", "Asset Details"))
		[
			EditingAssetWidget.ToSharedRef()
		];
}

TSharedRef<SDockTab> FPoseSearchDatabaseEditorToolkit::SpawnTab_PreviewSettings(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FPoseSearchDatabaseEditorTabs::PreviewSettingsID);

	FAdvancedPreviewSceneModule& AdvancedPreviewSceneModule = FModuleManager::LoadModuleChecked<FAdvancedPreviewSceneModule>("AdvancedPreviewScene");
	TSharedRef<SWidget> InWidget= AdvancedPreviewSceneModule.CreateAdvancedPreviewSceneSettingsWidget(PreviewScene.ToSharedRef());

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		[
			InWidget
		];

	return SpawnedTab;
}

void FPoseSearchDatabaseEditorToolkit::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
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

#undef LOCTEXT_NAMESPACE
