// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorToolkit.h"
#include "ChaosClothAsset/ChaosClothAssetEditorModule.h"
#include "ChaosClothAsset/ClothEditor.h"
#include "ChaosClothAsset/ClothEditorMode.h"
#include "ChaosClothAsset/ClothEditorModeUILayer.h"
#include "ChaosClothAsset/SClothEditor3DViewport.h"
#include "ChaosClothAsset/ClothEditor3DViewportClient.h"
#include "ChaosClothAsset/SClothEditorRestSpaceViewport.h"
#include "ChaosClothAsset/ClothEditorRestSpaceViewportClient.h"
#include "Framework/Docking/LayoutExtender.h"
#include "AssetEditorModeManager.h"
#include "AdvancedPreviewScene.h"
#include "SAssetEditorViewport.h"
#include "EditorViewportTabContent.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ChaosClothAssetEditorToolkit"

const FName FChaosClothAssetEditorToolkit::ClothPreviewTabID(TEXT("ChaosClothAssetEditor_ClothPreviewTab"));

FChaosClothAssetEditorToolkit::FChaosClothAssetEditorToolkit(UAssetEditor* InOwningAssetEditor)
	: FBaseCharacterFXEditorToolkit(InOwningAssetEditor, FName("ChaosClothAssetEditor"))
{
	check(Cast<UChaosClothAssetEditor>(InOwningAssetEditor));

	// We will replace the StandaloneDefaultLayout that our parent class gave us with 
	// one where the properties detail panel is a vertical column on the left, and there
	// are two viewports on the right.
	// We define explicit ExtensionIds on the stacks to reference them later when the
	// UILayer provides layout extensions. 
	//
	// Note: Changes to the layout should include a increment to the layout's ID, i.e.
	// ChaosClothAssetEditorLayout[X] -> ChaosClothAssetEditorLayout[X+1]. Otherwise, layouts may be messed up
	// without a full reset to layout defaults inside the editor.
	StandaloneDefaultLayout = FTabManager::NewLayout(FName("ChaosClothAssetEditorLayout2"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					->SetExtensionId(UChaosClothAssetEditorUISubsystem::EditorSidePanelAreaName)
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(ViewportTabID, ETabState::OpenedTab)
					->SetExtensionId("RestSpaceViewportArea")
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(ClothPreviewTabID, ETabState::OpenedTab)
					->SetExtensionId("Viewport3DArea")
					->SetHideTabWell(true)
				)
			)
		);

	// Add any extenders specified by the UISubsystem
	// The extenders provide defined locations for FModeToolkit to attach
	// tool palette tabs and detail panel tabs
	LayoutExtender = MakeShared<FLayoutExtender>();
	FChaosClothAssetEditorModule* Module = &FModuleManager::LoadModuleChecked<FChaosClothAssetEditorModule>("ChaosClothAssetEditor");
	Module->OnRegisterLayoutExtensions().Broadcast(*LayoutExtender);
	StandaloneDefaultLayout->ProcessExtensions(*LayoutExtender);

	FPreviewScene::ConstructionValues PreviewSceneArgs;
	PreviewSceneArgs.bShouldSimulatePhysics = 1;
	PreviewSceneArgs.bCreatePhysicsScene = 1;
	
	ClothPreviewScene = MakeUnique<FAdvancedPreviewScene>(PreviewSceneArgs);
	ClothPreviewScene->SetFloorVisibility(false, true);
	ClothPreviewEditorModeManager = MakeShared<FAssetEditorModeManager>();
	ClothPreviewEditorModeManager->SetPreviewScene(ClothPreviewScene.Get());

	//ClothPreviewInputRouter = ClothPreviewEditorModeManager->GetInteractiveToolsContext()->InputRouter;
	ClothPreviewTabContent = MakeShareable(new FEditorViewportTabContent());
	ClothPreviewViewportClient = MakeShared<FChaosClothAssetEditor3DViewportClient>(ClothPreviewEditorModeManager.Get(), ClothPreviewScene.Get());

	ClothPreviewViewportDelegate = [this](FAssetEditorViewportConstructionArgs InArgs)
	{
		return SNew(SChaosClothAssetEditor3DViewport, InArgs)
			.EditorViewportClient(ClothPreviewViewportClient);
	};

	// This API object serves as a communication point between the viewport toolbars and the tools.
	// We create it here so that we can pass it both into the rest space viewport and when we initialize 
	// the mode.
	//ViewportButtonsAPI = NewObject<UClothToolViewportButtonsAPI>();


	FPreviewScene::ConstructionValues SceneArgs;
	ObjectScene = MakeUnique<FPreviewScene>(SceneArgs);

}

FChaosClothAssetEditorToolkit::~FChaosClothAssetEditorToolkit()
{
	// We need to force the cloth editor mode deletion now because otherwise the preview and rest-space worlds
	// will end up getting destroyed before the mode's Exit() function gets to run, and we'll get some
	// warnings when we destroy any mode actors.
	EditorModeManager->DestroyMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId);
}

// This gets used to label the editor's tab in the window that opens.
FText FChaosClothAssetEditorToolkit::GetToolkitName() const
{
	const TArray<UObject*>* Objects = GetObjectsCurrentlyBeingEdited();
	if (Objects->Num() == 1)
	{
		return FText::Format(LOCTEXT("ChaosClothAssetEditorTabNameWithObject", "Cloth: {0}"), GetLabelForObject((*Objects)[0]));
	}
	return LOCTEXT("ChaosClothAssetEditorMultipleTabName", "Cloth: Multiple");
}

FName FChaosClothAssetEditorToolkit::GetToolkitFName() const
{
	return FName("Cloth Editor");
}

// TODO: What is this actually used for?
FText FChaosClothAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ChaosClothAssetEditorBaseName", "Cloth Editor");
}

FText FChaosClothAssetEditorToolkit::GetToolkitToolTipText() const
{
	FString ToolTipString;
	ToolTipString += LOCTEXT("ToolTipAssetLabel", "Asset").ToString();
	ToolTipString += TEXT(": ");

	const TArray<UObject*>* Objects = GetObjectsCurrentlyBeingEdited();
	check(Objects && Objects->Num() > 0);
	ToolTipString += GetLabelForObject((*Objects)[0]).ToString();
	for (int32 i = 1; i < Objects->Num(); ++i)
	{
		ToolTipString += TEXT(", ");
		ToolTipString += GetLabelForObject((*Objects)[i]).ToString();
	}

	return FText::FromString(ToolTipString);
}

void FChaosClothAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	// We bypass FBaseAssetToolkit::RegisterTabSpawners because it doesn't seem to provide us with
	// anything except tabs that we don't want.
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	EditorMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_ChaosClothAssetEditor", "Cloth Editor"));

	// Here we set up the tabs we referenced in StandaloneDefaultLayout (in the constructor).
	// We don't deal with the toolbar palette here, since this is handled by existing
    // infrastructure in FModeToolkit. We only setup spawners for our custom tabs, namely
    // the 2D and 3D viewports.
	InTabManager->RegisterTabSpawner(ClothPreviewTabID, FOnSpawnTab::CreateSP(this, &FChaosClothAssetEditorToolkit::SpawnTab_ClothPreview))
		.SetDisplayName(LOCTEXT("3DViewportTabLabel", "Cloth 3D Preview Viewport"))
		.SetGroup(EditorMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(ViewportTabID, FOnSpawnTab::CreateSP(this, &FChaosClothAssetEditorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("RestSpaceViewportTabLabel", "Cloth Rest Space Viewport"))
		.SetGroup(EditorMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

bool FChaosClothAssetEditorToolkit::OnRequestClose()
{
	// Note: This needs a bit of adjusting, because currently OnRequestClose seems to be 
	// called multiple times when the editor itself is being closed. We can take the route 
	// of NiagaraScriptToolkit and remember when changes are discarded, but this can cause
	// issues if the editor close sequence is interrupted due to some other asset editor.

	UChaosClothAssetEditorMode* ClothEdMode = Cast<UChaosClothAssetEditorMode>(EditorModeManager->GetActiveScriptableMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId));
	if (!ClothEdMode) {
		// If we don't have a valid mode, because the OnRequestClose is currently being called multiple times,
		// simply return true because there's nothing left to do.
		return true; 
	}

	// Warn the user of any unapplied changes.
	if (ClothEdMode->HaveUnappliedChanges())
	{
		TArray<TObjectPtr<UObject>> UnappliedAssets;
		ClothEdMode->GetAssetsWithUnappliedChanges(UnappliedAssets);

		EAppReturnType::Type YesNoCancelReply = FMessageDialog::Open(EAppMsgType::YesNoCancel,
			NSLOCTEXT("ChaosClothAssetEditor", "Prompt_ChaosClothAssetEditorClose", "At least one of the assets has unapplied changes. Would you like to apply them? (Selecting 'No' will cause all changes to be lost!)"));

		switch (YesNoCancelReply)
		{
		case EAppReturnType::Yes:
			ClothEdMode->ApplyChanges();
			break;

		case EAppReturnType::No:
			// exit
			break;

		case EAppReturnType::Cancel:
			// don't exit
			return false;
		}
	}

	// Give any active modes a chance to shutdown while the toolkit host is still alive
    // This is super important to do, otherwise currently opened tabs won't be marked as "closed".
    // This results in tabs not being properly recycled upon reopening the editor and tab
    // duplication for each opening event.
	GetEditorModeManager().ActivateDefaultMode();

	return FAssetEditorToolkit::OnRequestClose();
}

void FChaosClothAssetEditorToolkit::AddViewportOverlayWidget(TSharedRef<SWidget> InViewportOverlayWidget) 
{
	TSharedPtr<SChaosClothAssetEditorRestSpaceViewport> ViewportWidget = StaticCastSharedPtr<SChaosClothAssetEditorRestSpaceViewport>(ViewportTabContent->GetFirstViewport());
	ViewportWidget->AddOverlayWidget(InViewportOverlayWidget);
}

void FChaosClothAssetEditorToolkit::RemoveViewportOverlayWidget(TSharedRef<SWidget> InViewportOverlayWidget)
{
	TSharedPtr<SChaosClothAssetEditorRestSpaceViewport> ViewportWidget = StaticCastSharedPtr<SChaosClothAssetEditorRestSpaceViewport>(ViewportTabContent->GetFirstViewport());
	ViewportWidget->RemoveOverlayWidget(InViewportOverlayWidget);
}

//// We override the "Save" button behavior slightly to apply our changes before saving the asset.
//void FChaosClothAssetEditorToolkit::SaveAsset_Execute()
//{
//	UChaosClothAssetEditorMode* ClothMode = Cast<UChaosClothAssetEditorMode>(EditorModeManager->GetActiveScriptableMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId));
//	check(ClothMode);
//	if (ClothMode->HaveUnappliedChanges())
//	{
//		ClothMode->ApplyChanges();
//	}
//
//	FAssetEditorToolkit::SaveAsset_Execute();
//}

TSharedRef<SDockTab> FChaosClothAssetEditorToolkit::SpawnTab_ClothPreview(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> DockableTab = SNew(SDockTab);
	const FString LayoutId = FString("ChaosClothAssetEditorClothPreviewViewport");
	ClothPreviewTabContent->Initialize(ClothPreviewViewportDelegate, DockableTab, LayoutId);
	return DockableTab;
}

//// This is bound in RegisterTabSpawners() to create the panel on the left. The panel is filled in by the mode.
//TSharedRef<SDockTab> FChaosClothAssetEditorToolkit::SpawnTab_InteractiveToolsPanel(const FSpawnTabArgs& Args)
//{
//	TSharedRef<SDockTab> ToolsPanel = SNew(SDockTab);
//
//	UChaosClothAssetEditorMode* ClothMode = Cast<UChaosClothAssetEditorMode>(EditorModeManager->GetActiveScriptableMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId));
//	if (!ClothMode)
//	{
//		// This is where we will drop out on the first call to this callback, when the mode does not yet
//		// exist. There is probably a place where we could safely intialize the mode to make sure that
//		// it is around before the first call, but it seems cleaner to just do the mode initialization 
//		// in PostInitAssetEditor and fill in the tab at that time.
//		// Later calls to this callback will occur if the user closes and restores the tab, and they
//		// will continue past this point to allow the tab to be refilled.
//		return ToolsPanel;
//	}
//	TSharedPtr<FModeToolkit> ClothModeToolkit = ClothMode->GetToolkit().Pin();
//	if (!ClothModeToolkit.IsValid())
//	{
//		return ToolsPanel;
//	}
//
//	ToolsPanel->SetContent(ClothModeToolkit->GetInlineContent().ToSharedRef());
//	return ToolsPanel;
//}

// Called from FBaseAssetToolkit::CreateWidgets to populate ViewportClient, but otherwise only used 
// in our own viewport delegate.
TSharedPtr<FEditorViewportClient> FChaosClothAssetEditorToolkit::CreateEditorViewportClient() const
{
	// Note that we can't reliably adjust the viewport client here because we will be passing it
	// into the viewport created by the viewport delegate we get from GetViewportDelegate(), and
	// that delegate may (will) affect the settings based on FAssetEditorViewportConstructionArgs,
	// namely ViewportType.
	// Instead, we do viewport client adjustment in PostInitAssetEditor().
	check(EditorModeManager.IsValid());
	return MakeShared<FChaosClothEditorRestSpaceViewportClient>(EditorModeManager.Get(), ObjectScene.Get());
}

// Called from FBaseAssetToolkit::CreateWidgets. The delegate call path goes through FAssetEditorToolkit::InitAssetEditor
// and FBaseAssetToolkit::SpawnTab_Viewport.
AssetEditorViewportFactoryFunction FChaosClothAssetEditorToolkit::GetViewportDelegate()
{
	AssetEditorViewportFactoryFunction TempViewportDelegate = [this](FAssetEditorViewportConstructionArgs InArgs)
	{
		return SAssignNew(RestSpaceViewport, SChaosClothAssetEditorRestSpaceViewport, InArgs)
			.EditorViewportClient(ViewportClient);
	};

	return TempViewportDelegate;
}


void FChaosClothAssetEditorToolkit::PostInitAssetEditor()
{
	FBaseCharacterFXEditorToolkit::PostInitAssetEditor();

	// Custom viewport setup

	auto SetCommonViewportClientOptions = [](FEditorViewportClient* Client)
	{
		// Normally the bIsRealtime flag is determined by whether the connection is remote, but our
		// tools require always being ticked.
		Client->SetRealtime(true);

		// Disable motion blur effects that cause our renders to "fade in" as things are moved
		Client->EngineShowFlags.SetTemporalAA(false);
		Client->EngineShowFlags.SetAntiAliasing(true);
		Client->EngineShowFlags.SetMotionBlur(false);

		// Disable the dithering of occluded portions of gizmos.
		Client->EngineShowFlags.SetOpaqueCompositeEditorPrimitives(true);

		// Disable hardware occlusion queries, which make it harder to use vertex shaders to pull materials
		// toward camera for z ordering because non-translucent materials start occluding themselves (once
		// the component bounds are behind the displaced geometry).
		Client->EngineShowFlags.SetDisableOcclusionQueries(true);
	};

	{
		// when CreateEditorViewportClient() is called, RestSpaceViewport is null. Set it here instead
		TSharedPtr<FChaosClothEditorRestSpaceViewportClient> VC = StaticCastSharedPtr<FChaosClothEditorRestSpaceViewportClient>(ViewportClient);
		VC->SetEditorViewportWidget(RestSpaceViewport);
	}
	
	SetCommonViewportClientOptions(ViewportClient.Get());

	// Ortho has too many problems with rendering things, unfortunately, so we should use perspective.
	ViewportClient->SetViewportType(ELevelViewportType::LVT_Perspective);

	// Lit gives us the most options in terms of the materials we can use.
	ViewportClient->SetViewMode(EViewModeIndex::VMI_Lit);

	// If exposure isn't set to fixed, it will flash as we stare into the void
	ViewportClient->ExposureSettings.bFixed = true;

	UChaosClothAssetEditorMode* ClothMode = Cast<UChaosClothAssetEditorMode>(EditorModeManager->GetActiveScriptableMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId));
	check(ClothMode);
	const TWeakPtr<FViewportClient> WeakViewportClient(ViewportClient);
	ClothMode->SetRestSpaceViewportClient(StaticCastWeakPtr<FChaosClothEditorRestSpaceViewportClient>(WeakViewportClient));
	ClothMode->RefocusRestSpaceViewportClient();

	// We need the viewport client to start out focused, or else it won't get ticked until
	// we click inside it.
	ViewportClient->ReceivedFocus(ViewportClient->Viewport);

	// Set up 3D viewport
	SetCommonViewportClientOptions(ClothPreviewViewportClient.Get());
	ClothPreviewViewportClient->SetInitialViewTransform(ELevelViewportType::LVT_Perspective, FVector(0, -100, 100), FRotator(0, 90, 0), DEFAULT_ORTHOZOOM);

	FBox PreviewBounds = ClothMode->PreviewBoundingBox();
	ClothPreviewViewportClient->FocusViewportOnBox(PreviewBounds);

}


FEditorModeID FChaosClothAssetEditorToolkit::GetEditorModeId() const
{
	return UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId;
}


void FChaosClothAssetEditorToolkit::InitializeEdMode(UBaseCharacterFXEditorMode* EdMode)
{
	UChaosClothAssetEditorMode* ClothMode = Cast<UChaosClothAssetEditorMode>(EdMode);
	check(ClothMode);

	// The mode will need to be able to get to the live preview world, camera, input router, and viewport buttons.
	ClothMode->InitializeContexts(*ClothPreviewViewportClient, *ClothPreviewEditorModeManager);

	TArray<TObjectPtr<UObject>> ObjectsToEdit;
	OwningAssetEditor->GetObjectsToEdit(ObjectsToEdit);

	EdMode->InitializeTargets(ObjectsToEdit);
}

void FChaosClothAssetEditorToolkit::CreateEditorModeUILayer()
{
	TSharedPtr<class IToolkitHost> PinnedToolkitHost = ToolkitHost.Pin();
	check(PinnedToolkitHost.IsValid());
	ModeUILayer = MakeShared<FChaosClothAssetEditorModeUILayer>(PinnedToolkitHost.Get());
}


#undef LOCTEXT_NAMESPACE
