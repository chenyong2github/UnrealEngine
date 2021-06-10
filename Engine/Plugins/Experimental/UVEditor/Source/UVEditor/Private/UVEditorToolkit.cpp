// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorToolkit.h"

#include "AdvancedPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "ContextObjectStore.h"
#include "EditorViewportTabContent.h"
#include "Misc/MessageDialog.h"
#include "PreviewScene.h"
#include "SAssetEditorViewport.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "ToolMenus.h"
#include "UVEditor.h"
#include "UVEditorMode.h"
#include "UVEditorCommands.h"
#include "UVEditorSubsystem.h"
#include "UVEditor2DViewportClient.h"
#include "UVToolContextObjects.h"
#include "Widgets/Docking/SDockTab.h"

#include "EdModeInteractiveToolsContext.h"

#define LOCTEXT_NAMESPACE "UVEditorToolkit"

const FName FUVEditorToolkit::InteractiveToolsPanelTabID(TEXT("UVEditor_InteractiveToolsTab"));
const FName FUVEditorToolkit::LivePreviewTabID(TEXT("UVEditor_LivePreviewTab"));

FUVEditorToolkit::FUVEditorToolkit(UAssetEditor* InOwningAssetEditor)
	: FBaseAssetToolkit(InOwningAssetEditor)
{
	check(Cast<UUVEditor>(InOwningAssetEditor));

	// We will replace the StandaloneDefaultLayout that our parent class gave us with 
	// one where the properties detail panel is a vertical column on the left, and there
	// are two viewports on the right.
	StandaloneDefaultLayout = FTabManager::NewLayout(FName("UVEditorLayout1"))
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
					->AddTab(InteractiveToolsPanelTabID, ETabState::OpenedTab)
					->SetHideTabWell(false)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(ViewportTabID, ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(LivePreviewTabID, ETabState::OpenedTab)
					->SetHideTabWell(false)
				)
			)
		);

	// We could create the preview scenes in CreateEditorViewportClient() the way that FBaseAssetToolkit
	// does, but it seems more intuitive to create them right off the bat and pass it in later. 
	FPreviewScene::ConstructionValues PreviewSceneArgs;
	UnwrapScene = MakeUnique<FPreviewScene>(PreviewSceneArgs);
	LivePreviewScene = MakeUnique<FAdvancedPreviewScene>(PreviewSceneArgs);
	LivePreviewScene->SetFloorVisibility(false, true);

	LivePreviewEditorModeManager = MakeShared<FAssetEditorModeManager>();
	LivePreviewEditorModeManager->SetPreviewScene(LivePreviewScene.Get());
	LivePreviewInputRouter = LivePreviewEditorModeManager->GetInteractiveToolsContext()->InputRouter;

	LivePreviewTabContent = MakeShareable(new FEditorViewportTabContent());
	LivePreviewViewportClient = MakeShared<FEditorViewportClient>(
		LivePreviewEditorModeManager.Get(), LivePreviewScene.Get());

	LivePreviewViewportDelegate = [this](FAssetEditorViewportConstructionArgs InArgs)
	{
		return SNew(SAssetEditorViewport, InArgs)
			.EditorViewportClient(LivePreviewViewportClient);
	};
}

FUVEditorToolkit::~FUVEditorToolkit()
{
	// We need to force the uv editor mode deletion now because otherwise the preview and unwrap worlds
	// will end up getting destroyed before the mode's Exit() function gets to run, and we'll get some
	// warnings when we destroy any mode actors.
	EditorModeManager->DestroyMode(UUVEditorMode::EM_UVEditorModeId);

	// The UV subsystem is responsible for opening/focusing UV editor instances, so we should
	// notify it that this one is closing.
	UUVEditorSubsystem* UVSubsystem = GEditor->GetEditorSubsystem<UUVEditorSubsystem>();
	if (UVSubsystem)
	{
		TArray<TObjectPtr<UObject>> ObjectsWeWereEditing;
		OwningAssetEditor->GetObjectsToEdit(ObjectsWeWereEditing);
		UVSubsystem->NotifyThatUVEditorClosed(ObjectsWeWereEditing);
	}
}

// This gets used to label the editor's tab in the window that opens.
FText FUVEditorToolkit::GetToolkitName() const
{
	const TArray<UObject*>* Objects = GetObjectsCurrentlyBeingEdited();
	if (Objects->Num() == 1)
	{
		return FText::Format(LOCTEXT("UVEditorTabNameWithObject", "UVs: {0}"), 
			GetLabelForObject((*Objects)[0]));
	}
	return LOCTEXT("UVEditorMultipleTabName", "UVs: Multiple");
}

// This gets used multiple places, most notably in GetToolMenuAppName, which gets
// used to refer to menus/toolbars internally.
FName FUVEditorToolkit::GetToolkitFName() const
{
	return FName("UVEditor");
}

// TODO: What is this actually used for?
FText FUVEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("UVBaseToolkitName", "UV");
}

FText FUVEditorToolkit::GetToolkitToolTipText() const
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

void FUVEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	// We bypass FBaseAssetToolkit::RegisterTabSpawners because it doesn't seem to provide us with
	// anything except tabs that we don't want.
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	// Here we set up the tabs we referenced in StandaloneDefaultLayout (in the constructor).

	InTabManager->RegisterTabSpawner(InteractiveToolsPanelTabID, FOnSpawnTab::CreateSP(this, 
		&FUVEditorToolkit::SpawnTab_InteractiveToolsPanel))
		.SetDisplayName(LOCTEXT("InteractiveToolsPanel", "Tools Panel"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(ViewportTabID, FOnSpawnTab::CreateSP(this, &FUVEditorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(LivePreviewTabID, FOnSpawnTab::CreateSP(this, 
		&FUVEditorToolkit::SpawnTab_LivePreview))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

bool FUVEditorToolkit::OnRequestClose()
{
	// Note: This needs a bit of adjusting, because currently OnRequestClose seems to be 
	// called multiple times when the editor itself is being closed. We can take the route 
	// of NiagaraScriptToolkit and remember when changes are discarded, but this can cause
	// issues if the editor close sequence is interrupted due to some other asset editor.

	UUVEditorMode* UVMode = Cast<UUVEditorMode>(EditorModeManager->GetActiveScriptableMode(UUVEditorMode::EM_UVEditorModeId));
	check(UVMode);

	// Warn the user of any unapplied changes.
	if (UVMode->HaveUnappliedChanges())
	{
		TArray<TObjectPtr<UObject>> UnappliedAssets;
		UVMode->GetAssetsWithUnappliedChanges(UnappliedAssets);

		EAppReturnType::Type YesNoCancelReply = FMessageDialog::Open(EAppMsgType::YesNoCancel,
			NSLOCTEXT("UVEditor", "Prompt_UVEditorClose", "At least one of the assets has unapplied changes. Would you like to apply them? (Selecting 'No' will cause all changes to be lost!)"));

		switch (YesNoCancelReply)
		{
		case EAppReturnType::Yes:
			UVMode->ApplyChanges();
			break;

		case EAppReturnType::No:
			// exit
			break;

		case EAppReturnType::Cancel:
			// don't exit
			return false;
		}
	}

	return FAssetEditorToolkit::OnRequestClose();
}

// We override the "Save" button behavior slightly to apply our changes before saving the asset.
void FUVEditorToolkit::SaveAsset_Execute()
{
	UUVEditorMode* UVMode = Cast<UUVEditorMode>(EditorModeManager->GetActiveScriptableMode(UUVEditorMode::EM_UVEditorModeId));
	check(UVMode);
	if (UVMode->HaveUnappliedChanges())
	{
		UVMode->ApplyChanges();
	}

	FAssetEditorToolkit::SaveAsset_Execute();
}

TSharedRef<SDockTab> FUVEditorToolkit::SpawnTab_LivePreview(const FSpawnTabArgs& Args)
{
	TSharedRef< SDockTab > DockableTab =
		SNew(SDockTab);

	const FString LayoutId = FString("UVEditorLivePreviewViewport");
	LivePreviewTabContent->Initialize(LivePreviewViewportDelegate, DockableTab, LayoutId);
	return DockableTab;
}

// This is bound in RegisterTabSpawners() to create the panel on the left. The panel is filled in by the mode.
TSharedRef<SDockTab> FUVEditorToolkit::SpawnTab_InteractiveToolsPanel(const FSpawnTabArgs& Args)
{
	ToolsPanel = SNew(SDockTab)
		.Label(LOCTEXT("UVToolPanelTitle", "UV Tools"));

	return ToolsPanel.ToSharedRef();
}

void FUVEditorToolkit::CreateWidgets()
{
	// This gets called during UAssetEditor::Init() after creation of the toolkit but before
	// calling InitAssetEditor on it. If we have custom mode-level toolbars we want to add,
	// they could potentially go here, but we still need to call the base CreateWidgets as well
	// because that calls things that make viewport client, etc.

	FBaseAssetToolkit::CreateWidgets();
}

// Called from FBaseAssetToolkit::CreateWidgets to populate ViewportClient, but otherwise only used 
// in our own viewport delegate.
TSharedPtr<FEditorViewportClient> FUVEditorToolkit::CreateEditorViewportClient() const
{
	// Note that we can't reliably adjust the viewport client here because we will be passing it
	// into the viewport created by the viewport delegate we get from GetViewportDelegate(), and
	// that delegate may (will) affect the settings based on FAssetEditorViewportConstructionArgs,
	// namely ViewportType.
	// Instead, we do viewport client adjustment in PostInitAssetEditor().
	check(EditorModeManager.IsValid());
	return MakeShared<FUVEditor2DViewportClient>(EditorModeManager.Get(), UnwrapScene.Get());
}

// Called from FBaseAssetToolkit::CreateWidgets. The delegate call path goes through FAssetEditorToolkit::InitAssetEditor
// and FBaseAssetToolkit::SpawnTab_Viewport.
AssetEditorViewportFactoryFunction FUVEditorToolkit::GetViewportDelegate()
{
	AssetEditorViewportFactoryFunction TempViewportDelegate = [this](FAssetEditorViewportConstructionArgs InArgs)
	{
		return SNew(SAssetEditorViewport, InArgs)
			.EditorViewportClient(ViewportClient);
	};

	return TempViewportDelegate;
}

// Called from FBaseAssetToolkit::CreateWidgets.
void FUVEditorToolkit::CreateEditorModeManager()
{
	EditorModeManager = MakeShared<FAssetEditorModeManager>();

	// The mode manager is the authority on what the world is for the mode and the tools context,
	// and setting the preview scene here makes our GetWorld() function return the preview scene
	// world instead of the normal level editor one. Important because that is where we create
	// any preview meshes, gizmo actors, etc.
	StaticCastSharedPtr<FAssetEditorModeManager>(EditorModeManager)->SetPreviewScene(UnwrapScene.Get());
}

void FUVEditorToolkit::PostInitAssetEditor()
{
	// Currently, aside from setting up all the UI elements, the toolkit also kicks off the UV
	// editor mode, which is the mode that the editor always works in (things are packaged into
	// a mode so that they can be moved to another asset editor if necessary).
	// We need the UV mode to be active to create the toolbox on the left.
	check(EditorModeManager.IsValid());
	EditorModeManager->ActivateMode(UUVEditorMode::EM_UVEditorModeId);
	UUVEditorMode* UVMode = Cast<UUVEditorMode>(
		EditorModeManager->GetActiveScriptableMode(UUVEditorMode::EM_UVEditorModeId));
	check(UVMode);

	// The mode will need to be able to get to the live preview world and input.
	UContextObjectStore* ContextStore = EditorModeManager->GetInteractiveToolsContext()->ToolManager->GetContextObjectStore();
	UUVToolLivePreviewAPI* LivePreviewAPI = NewObject<UUVToolLivePreviewAPI>();
	LivePreviewAPI->Initialize(LivePreviewScene->GetWorld(), LivePreviewInputRouter);
	ContextStore->AddContextObject(LivePreviewAPI);

	// Initialize mode state.
	TArray<TObjectPtr<UObject>> ObjectsToEdit;
	OwningAssetEditor->GetObjectsToEdit(ObjectsToEdit);

	// TODO: get these when possible, set them otherwise.
	TArray<FTransform> ObjectTransforms;
	ObjectTransforms.SetNum(ObjectsToEdit.Num());

	UVMode->InitializeTargets(ObjectsToEdit, &ObjectTransforms);

	// Plug in the mode tool panel
	TSharedPtr<FModeToolkit> UVModeToolkit = UVMode->GetToolkit().Pin();
	check(UVModeToolkit.IsValid());
	ToolsPanel->SetContent(UVModeToolkit->GetInlineContent().ToSharedRef());

	// Add the "Apply Changes" button. It should actually be safe to do this almost
	// any time, even before that toolbar's registration, but it's easier to put most
	// things into PostInitAssetEditor().

	// TODO: Use the icon that material editor uses. Also, the space between the button
	// sections is too large, but the button can't go into the default toolbar section or
	// else it will show up in other asset editors where the section is used.
	// TODO: We may consider putting actions like these, which are tied to a mode, into
	// some list of mode actions, and then letting the mode supply them to the owning
	// asset editor on enter/exit. Revisit when/if this becomes easier to do.
	ToolkitCommands->MapAction(
		FUVEditorCommands::Get().ApplyChanges,
		FExecuteAction::CreateUObject(UVMode, &UUVEditorMode::ApplyChanges),
		FCanExecuteAction::CreateUObject(UVMode, &UUVEditorMode::HaveUnappliedChanges));
	FName ParentToolbarName;
	const FName ToolBarName = GetToolMenuToolbarName(ParentToolbarName);
	UToolMenu* AssetToolbar = UToolMenus::Get()->ExtendMenu(ToolBarName);
	FToolMenuSection& Section = AssetToolbar->FindOrAddSection("UVAsset");
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(FUVEditorCommands::Get().ApplyChanges));


	// Adjust our main (2D) viewport:

	// Ortho has too many problems with rendering things, unfortunately, so we should use perspective.
	ViewportClient->SetViewportType(ELevelViewportType::LVT_Perspective);

	// Lit gives us the most options in terms of the materials we can use.
	ViewportClient->SetViewMode(EViewModeIndex::VMI_Lit);

	// scale [0,1] to [0,ScaleFactor].
	// We set our camera to look downward, centered, far enough to be able to see the edges
	// with a 90 degree FOV
	double ScaleFactor = 1;
	UUVEditorSubsystem* UVSubsystem = GEditor->GetEditorSubsystem<UUVEditorSubsystem>();
	if (UVSubsystem)
	{
		ScaleFactor = UUVEditorMode::GetUVMeshScalingFactor();
	}
	ViewportClient->SetViewLocation(FVector(ScaleFactor / 2, ScaleFactor/2, ScaleFactor));
	ViewportClient->SetViewRotation(FRotator(-90, 0, 0));

	// If exposure isn't set to fixed, it will flash as we stare into the void
	ViewportClient->ExposureSettings.bFixed = true;

	// TODO: Disable temporal aa or whatever else is blurring the lines as the camera moves.

	// We need the viewport client to start out focused, or else it won't get ticked until
	// we click inside it.
	ViewportClient->ReceivedFocus(ViewportClient->Viewport);


	// Adjust our live preview (3D) viewport
	// TODO: This should not be hardcoded
	LivePreviewViewportClient->SetViewLocation(FVector(-200, 100, 100));
	LivePreviewViewportClient->SetLookAtLocation(FVector(0, 0, 0));
	LivePreviewViewportClient->ToggleOrbitCamera(true);
}

#undef LOCTEXT_NAMESPACE