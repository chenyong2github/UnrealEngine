// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorToolkit.h"

#include "AssetEditorModeManager.h"
#include "Misc/MessageDialog.h"
#include "PreviewScene.h"
#include "SAssetEditorViewport.h"
#include "ToolMenus.h"
#include "UVEditor.h"
#include "UVEditorMode.h"
#include "UVEditorCommands.h"
#include "UVEditorSubsystem.h"
#include "UVEditor2DViewportClient.h"
#include "Widgets/Docking/SDockTab.h"

#include "EdModeInteractiveToolsContext.h"

#define LOCTEXT_NAMESPACE "UVEditorToolkit"

const FName FUVEditorToolkit::InteractiveToolsPanelTabID(TEXT("UVEditor_InteractiveToolsTab"));

FUVEditorToolkit::FUVEditorToolkit(UAssetEditor* InOwningAssetEditor)
	: FBaseAssetToolkit(InOwningAssetEditor)
{
	check(Cast<UUVEditor>(InOwningAssetEditor));

	// TODO: Will eventually need another viewport/previewscene for a 3d preview of the mesh

	// We will replace the StandaloneDefaultLayout that our parent class gave us with 
	// one where the properties detail panel is a vertical column on the left, and the
	// viewport is on the right.
	StandaloneDefaultLayout = FTabManager::NewLayout(FName("UVEditorLayout"))
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
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.7f)
					->AddTab(ViewportTabID, ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
			)
		);

	// We could create the preview scene in CreateEditorViewportClient() the way that FBaseAssetToolkit
	// does, but it seems more intuitive to create it right off the bat and pass it in later. The preview
	// scene is important because it holds the world in which our mode will create any objects to display.
	FPreviewScene::ConstructionValues PreviewSceneArgs;
	PreviewScene = MakeUnique<FPreviewScene>(PreviewSceneArgs);
}

FUVEditorToolkit::~FUVEditorToolkit()
{
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
	// Here we set up the tabs we referenced in StandaloneDefaultLayout (in the constructor).
	// The base class already creates the viewport tab (and also a details tab that we don't want).

	FBaseAssetToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(DetailsTabID);

	InTabManager->RegisterTabSpawner(InteractiveToolsPanelTabID, FOnSpawnTab::CreateSP(this, &FUVEditorToolkit::SpawnTab_InteractiveToolsPanel))
		.SetDisplayName(LOCTEXT("InteractiveToolsPanel", "Tools Panel"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));
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

// This is bound in RegisterTabSpawners() to create the panel on the left. It's filled in by the mode.
TSharedRef<SDockTab> FUVEditorToolkit::SpawnTab_InteractiveToolsPanel(const FSpawnTabArgs& Args)
{
	ToolsPanel = SNew(SDockTab)
		.Label(LOCTEXT("UVToolPanelTitle", "UV Tools"));

	return ToolsPanel.ToSharedRef();
}

// This is used to create the viewport itself
AssetEditorViewportFactoryFunction FUVEditorToolkit::GetViewportDelegate()
{
	AssetEditorViewportFactoryFunction TempViewportDelegate = [this](FAssetEditorViewportConstructionArgs InArgs)
	{
		return SNew(SAssetEditorViewport, InArgs)
			.EditorViewportClient(ViewportClient);
	};

	return TempViewportDelegate;
}

TSharedPtr<FEditorViewportClient> FUVEditorToolkit::CreateEditorViewportClient() const
{
	// Note that we can't reliably adjust the viewport client here because we will be passing it
	// into the viewport created by the viewport delegate we get from GetViewportDelegate(), and
	// that delegate may (will) affect the settings based on FAssetEditorViewportConstructionArgs,
	// namely ViewportType.
	// Instead, we do viewport client adjustment in PostInitAssetEditor().
	check(EditorModeManager.IsValid());
	return MakeShared<FUVEditor2DViewportClient>(EditorModeManager.Get(), PreviewScene.Get());
}

void FUVEditorToolkit::CreateWidgets()
{
	// This gets called during UAssetEditor::Init() after creation of the toolkit but before
	// calling InitAssetEditor on it. If we have custom mode-level toolbars we want to add,
	// they could potentially go here, but we still need to call the base CreateWidgets as well.

	FBaseAssetToolkit::CreateWidgets();
}


void FUVEditorToolkit::CreateEditorModeManager()
{
	EditorModeManager = MakeShared<FAssetEditorModeManager>();

	// The mode manager is the authority on what the world is for the mode and the tools context,
	// and setting the preview scene here makes our GetWorld() function return the preview scene
	// world instead of the normal level editor one. Important because that is where we create
	// any preview meshes, gizmo actors, etc.
	StaticCastSharedPtr<FAssetEditorModeManager>(EditorModeManager)->SetPreviewScene(PreviewScene.Get());
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

	// Initialize mode state.
	TArray<TObjectPtr<UObject>> ObjectsToEdit;
	OwningAssetEditor->GetObjectsToEdit(ObjectsToEdit);
	UVMode->InitializeTargets(ObjectsToEdit);

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


	// Adjust the viewport:

	// Ortho has too many problems with rendering things, unfortunately, so we should use perspective.
	ViewportClient->SetViewportType(ELevelViewportType::LVT_Perspective);

	// Lit gives us the most options in terms of the materials we can use.
	ViewportClient->SetViewMode(EViewModeIndex::VMI_Lit);

	// TODO: This should be tied to wherever we store the settings for UV scaling. Right now we
	// scale [0,1] to [0,1000], hence these settings.
	ViewportClient->SetViewLocation(FVector(500, 500, 1000));
	ViewportClient->SetViewRotation(FRotator(-90, 0, 0));

	// If exposure isn't set to fixed, it will flash as we stare into the void
	ViewportClient->ExposureSettings.bFixed = true;

	// TODO: Disable temporal aa or whatever else is blurring the lines as the camera moves.

	// We need the viewport client to start out focused, or else it won't get ticked until
	// we click inside it.
	ViewportClient->ReceivedFocus(ViewportClient->Viewport);
}

#undef LOCTEXT_NAMESPACE