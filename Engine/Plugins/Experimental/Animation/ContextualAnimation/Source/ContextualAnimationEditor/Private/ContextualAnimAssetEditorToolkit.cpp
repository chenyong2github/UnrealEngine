// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimAssetEditorToolkit.h"
#include "SContextualAnimViewport.h"
#include "SContextualAnimAssetBrowser.h"
#include "ContextualAnimPreviewScene.h"
#include "ContextualAnimAssetEditorCommands.h"
#include "Widgets/Docking/SDockTab.h"
#include "GameFramework/WorldSettings.h"
#include "ContextualAnimViewModel.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimMovieSceneSequence.h"
#include "ISequencerModule.h"
#include "ISequencer.h"
#include "MovieScene.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AdvancedPreviewSceneModule.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "ContextualAnimEditorTypes.h"
#include "Widgets/Input/SButton.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ContextualAnimAssetEditorToolkit"

const FName ContextualAnimEditorAppName = FName(TEXT("ContextualAnimEditorApp"));

// Tab identifiers
struct FContextualAnimEditorTabs
{
	static const FName AssetDetailsID;
	static const FName ViewportID;
	static const FName TimelineID;
	static const FName AssetBrowserID;
	static const FName PreviewSettingsID;
};

const FName FContextualAnimEditorTabs::AssetDetailsID(TEXT("ContextualAnimEditorAssetDetailsTabID"));
const FName FContextualAnimEditorTabs::ViewportID(TEXT("ContextualAnimEditorViewportTabID"));
const FName FContextualAnimEditorTabs::TimelineID(TEXT("ContextualAnimEditorTimelineTabID"));
const FName FContextualAnimEditorTabs::AssetBrowserID(TEXT("ContextualAnimEditorAssetBrowserTabID"));
const FName FContextualAnimEditorTabs::PreviewSettingsID(TEXT("ContextualAnimEditorPreviewSettingsTabID"));

FContextualAnimAssetEditorToolkit::FContextualAnimAssetEditorToolkit()
{
}

FContextualAnimAssetEditorToolkit::~FContextualAnimAssetEditorToolkit()
{
}

UContextualAnimSceneAsset* FContextualAnimAssetEditorToolkit::GetSceneAsset() const
{
	return ViewModel.IsValid() ? ViewModel->GetSceneAsset() : nullptr;
}

void FContextualAnimAssetEditorToolkit::ResetPreviewScene()
{
	ViewModel->SetDefaultMode();
}

void FContextualAnimAssetEditorToolkit::InitAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UContextualAnimSceneAsset* SceneAsset)
{
	// Bind Commands
	BindCommands();

	// Create Preview Scene
	if (!PreviewScene.IsValid())
	{
		PreviewScene = MakeShareable(new FContextualAnimPreviewScene(FPreviewScene::ConstructionValues().AllowAudioPlayback(true).ShouldSimulatePhysics(true).ForceUseMovementComponentInNonGameWorld(true),
			StaticCastSharedRef<FContextualAnimAssetEditorToolkit>(AsShared())));

		//Temporary fix for missing attached assets - MDW (Copied from FPersonaToolkit::CreatePreviewScene)
		PreviewScene->GetWorld()->GetWorldSettings()->SetIsTemporarilyHiddenInEditor(false);
	}

	// Create viewport widget
	FContextualAnimViewportRequiredArgs ViewportArgs(StaticCastSharedRef<FContextualAnimAssetEditorToolkit>(AsShared()), PreviewScene.ToSharedRef());
	ViewportWidget = SNew(SContextualAnimViewport, ViewportArgs);

	// Create asset browser widget
	AssetBrowserWidget = SNew(SContextualAnimAssetBrowser);

	ViewModel = MakeShared<FContextualAnimViewModel>();
	ViewModel->Initialize(SceneAsset, PreviewScene.ToSharedRef());

	// Create Asset Details widget
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NotifyHook = this;

	EditingAssetWidget = PropertyModule.CreateDetailView(Args);
	EditingAssetWidget->SetObject(SceneAsset);
	EditingAssetWidget->OnFinishedChangingProperties().AddSP(this, &FContextualAnimAssetEditorToolkit::OnFinishedChangingProperties);

	// Define Editor Layout
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_ContextualAnimAnimEditor_Layout_v0.10")
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
						FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.65f)
							->AddTab(FContextualAnimEditorTabs::ViewportID, ETabState::OpenedTab)->SetHideTabWell(true)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.3f)
							->AddTab(FContextualAnimEditorTabs::TimelineID, ETabState::OpenedTab)
						)
					)
					->Split
					(
						FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.3f)
							->AddTab(FContextualAnimEditorTabs::AssetDetailsID, ETabState::OpenedTab)
							->AddTab(FContextualAnimEditorTabs::PreviewSettingsID, ETabState::OpenedTab)
						)
					)
				)
			)
		);

	const bool bCreateDefaultStandaloneMenuParam = true;
	const bool bCreateDefaultToolbarParam = true;
	const bool bIsToolbarFocusableParam = false;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, ContextualAnimEditorAppName, StandaloneDefaultLayout, bCreateDefaultStandaloneMenuParam, bCreateDefaultToolbarParam, SceneAsset, bIsToolbarFocusableParam);

	ExtendToolbar();

	RegenerateMenusAndToolbars();
}

void FContextualAnimAssetEditorToolkit::BindCommands()
{
	const FContextualAnimAssetEditorCommands& Commands = FContextualAnimAssetEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.ResetPreviewScene,
		FExecuteAction::CreateSP(this, &FContextualAnimAssetEditorToolkit::ResetPreviewScene),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.Simulate,
		FExecuteAction::CreateSP(this, &FContextualAnimAssetEditorToolkit::ToggleSimulateMode),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FContextualAnimAssetEditorToolkit::IsSimulateModeActive));
}

void FContextualAnimAssetEditorToolkit::ToggleSimulateMode()
{
	if(ViewModel.IsValid())
	{
		ViewModel->ToggleSimulateMode();
	}
}

bool FContextualAnimAssetEditorToolkit::IsSimulateModeActive() const
{
	return (ViewModel.IsValid()) ? ViewModel->IsSimulateModeActive() : false;
}

void FContextualAnimAssetEditorToolkit::ExtendToolbar()
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FContextualAnimAssetEditorToolkit::FillToolbar)
	);
}

void FContextualAnimAssetEditorToolkit::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.AddToolBarButton(
		FContextualAnimAssetEditorCommands::Get().ResetPreviewScene,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh")
	);

	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &FContextualAnimAssetEditorToolkit::BuildSectionsMenu),
		LOCTEXT("Sections_Label", "Sections"),
		FText::GetEmpty(),
		FSlateIcon()
	);

	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &FContextualAnimAssetEditorToolkit::BuildNewAnimSetWidget),
		LOCTEXT("NewSet_Label", "New Set"),
		FText::GetEmpty(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus")
	);

	ToolbarBuilder.AddToolBarButton(
		FContextualAnimAssetEditorCommands::Get().Simulate,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon()
	);
}

TSharedRef<SWidget> FContextualAnimAssetEditorToolkit::BuildSectionsMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, GetToolkitCommands());

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("Sections_Label", "Sections"));
	{
		const UContextualAnimSceneAsset* SceneAsset = GetSceneAsset();
		
		TArray<FName> SectionNames = SceneAsset->GetSectionNames();
		for (int32 Idx = 0; Idx < SectionNames.Num(); Idx++)
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(SectionNames[Idx].ToString()),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, Idx]() {
					ViewModel->SetActiveSection(Idx);
				})));
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FContextualAnimAssetEditorToolkit::BuildNewAnimSetWidget()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, GetToolkitCommands());

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.bAllowSearch = false;
	Args.bAllowFavoriteSystem = false;

	NewAnimSetWidgetStruct = MakeShared<FStructOnScope>(FContextualAnimNewAnimSetParams::StaticStruct());
	FContextualAnimNewAnimSetParams* Params = (FContextualAnimNewAnimSetParams*)NewAnimSetWidgetStruct->GetStructMemory();

	const TArray<FName> Roles = ViewModel->GetSceneAsset()->GetRoles();
	for (FName Role : Roles)
	{
		FContextualAnimNewAnimSetData Entry;
		Entry.RoleName = Role;
		Params->Data.Add(Entry);
	}

	TSharedRef<IStructureDetailsView> StructureDetailsView = PropertyModule.CreateStructureDetailView(Args, FStructureDetailsViewArgs(), NewAnimSetWidgetStruct);

	MenuBuilder.AddWidget(
		SNew(SBox)
		.MinDesiredWidth(500.0f)
		.MaxDesiredWidth(500.f)
		.MaxDesiredHeight(400.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				StructureDetailsView->GetWidget().ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(5)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
				.Text(LOCTEXT("OK", "OK"))
				.OnClicked_Lambda([this]()
				{
					FContextualAnimNewAnimSetParams* Params = (FContextualAnimNewAnimSetParams*)NewAnimSetWidgetStruct->GetStructMemory();
					check(Params);

					ViewModel->AddNewAnimSet(*Params);

					FSlateApplication::Get().DismissAllMenus();

					return FReply::Handled();
				})
			]
		], FText(), true, false);

	return MenuBuilder.MakeWidget();
}

void FContextualAnimAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_CASEditor", "Contextual Animation Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(FContextualAnimEditorTabs::ViewportID, FOnSpawnTab::CreateSP(this, &FContextualAnimAssetEditorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(FContextualAnimEditorTabs::AssetDetailsID, FOnSpawnTab::CreateSP(this, &FContextualAnimAssetEditorToolkit::SpawnTab_AssetDetails))
		.SetDisplayName(LOCTEXT("AssetDetailsTab", "AssetDetails"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(FContextualAnimEditorTabs::TimelineID, FOnSpawnTab::CreateSP(this, &FContextualAnimAssetEditorToolkit::SpawnTab_Timeline))
		.SetDisplayName(LOCTEXT("TimelineTab", "Timeline"))
		.SetGroup(WorkspaceMenuCategoryRef);

 	InTabManager->RegisterTabSpawner(FContextualAnimEditorTabs::AssetBrowserID, FOnSpawnTab::CreateSP(this, &FContextualAnimAssetEditorToolkit::SpawnTab_AssetBrowser))
 		.SetDisplayName(LOCTEXT("AssetBrowserTab", "AssetBrowser"))
 		.SetGroup(WorkspaceMenuCategoryRef);

	InTabManager->RegisterTabSpawner(FContextualAnimEditorTabs::PreviewSettingsID, FOnSpawnTab::CreateSP(this, &FContextualAnimAssetEditorToolkit::SpawnTab_PreviewSettings))
		.SetDisplayName(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FContextualAnimAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(FContextualAnimEditorTabs::ViewportID);
	InTabManager->UnregisterTabSpawner(FContextualAnimEditorTabs::AssetDetailsID);
	InTabManager->UnregisterTabSpawner(FContextualAnimEditorTabs::TimelineID);
	InTabManager->UnregisterTabSpawner(FContextualAnimEditorTabs::AssetBrowserID);
	InTabManager->UnregisterTabSpawner(FContextualAnimEditorTabs::PreviewSettingsID);
}

FName FContextualAnimAssetEditorToolkit::GetToolkitFName() const
{
	return FName("ContextualAnimEditor");
}

FText FContextualAnimAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ContextualAnimEditorAppLabel", "Contextual Anim Editor");
}

FText FContextualAnimAssetEditorToolkit::GetToolkitName() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), FText::FromString(GetSceneAsset()->GetName()));
	return FText::Format(LOCTEXT("ContextualAnimEditorToolkitName", "{AssetName}"), Args);
}

FLinearColor FContextualAnimAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

FString FContextualAnimAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return TEXT("ContextualAnimEditor");
}

TSharedRef<SDockTab> FContextualAnimAssetEditorToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FContextualAnimEditorTabs::ViewportID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab).Label(LOCTEXT("ViewportTab_Title", "Viewport"));

	if (ViewportWidget.IsValid())
	{
		SpawnedTab->SetContent(ViewportWidget.ToSharedRef());
	}

	return SpawnedTab;
}

TSharedRef<SDockTab> FContextualAnimAssetEditorToolkit::SpawnTab_Timeline(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == FContextualAnimEditorTabs::TimelineID);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			ViewModel->GetSequencer()->GetSequencerWidget()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FContextualAnimAssetEditorToolkit::SpawnTab_AssetDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FContextualAnimEditorTabs::AssetDetailsID);

	return SNew(SDockTab)
		.Label(LOCTEXT("AssetDetails_Title", "Asset Details"))
		[
			EditingAssetWidget.ToSharedRef()
		];
}

TSharedRef<SDockTab> FContextualAnimAssetEditorToolkit::SpawnTab_AssetBrowser(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FContextualAnimEditorTabs::AssetBrowserID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab).Label(LOCTEXT("EditorAssetBrowser_Title", "Asset Browser"));

	if (AssetBrowserWidget.IsValid())
	{
		SpawnedTab->SetContent(AssetBrowserWidget.ToSharedRef());
	}

	return SpawnedTab;
}

TSharedRef<SDockTab> FContextualAnimAssetEditorToolkit::SpawnTab_PreviewSettings(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FContextualAnimEditorTabs::PreviewSettingsID);

	FAdvancedPreviewSceneModule& AdvancedPreviewSceneModule = FModuleManager::LoadModuleChecked<FAdvancedPreviewSceneModule>("AdvancedPreviewScene");
	TSharedRef<SWidget> InWidget= AdvancedPreviewSceneModule.CreateAdvancedPreviewSceneSettingsWidget(PreviewScene.ToSharedRef());

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		[
			InWidget
		];

	return SpawnedTab;
}

void FContextualAnimAssetEditorToolkit::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	ViewModel->OnFinishedChangingProperties(PropertyChangedEvent);
}

#undef LOCTEXT_NAMESPACE

