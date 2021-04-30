// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "Misc/MessageDialog.h"
#include "Classes/EditorStyleSettings.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/WorldSettings.h"
#include "LevelEditorViewport.h"
#include "EditorModeRegistry.h"
#include "EditorModes.h"
#include "Engine/BookMark.h"
#include "EditorSupportDelegates.h"
#include "EdMode.h"
#include "Toolkits/IToolkitHost.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SButton.h"
#include "Engine/LevelStreaming.h"
#include "EditorWorldExtension.h"
#include "ViewportWorldInteraction.h"
#include "Editor/EditorEngine.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Bookmarks/IBookmarkTypeTools.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Toolkits/BaseToolkit.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/BrushEditingSubsystem.h"
#include "Tools/UEdMode.h"
#include "Widgets/Images/SImage.h"
#include "InputRouter.h"
#include "InteractiveGizmoManager.h"

/*------------------------------------------------------------------------------
	FEditorModeTools.

	The master class that handles tracking of the current mode.
------------------------------------------------------------------------------*/

const FName FEditorModeTools::EditorModeToolbarTabName = TEXT("EditorModeToolbar");

FEditorModeTools::FEditorModeTools()
	: PivotShown(false)
	, Snapping(false)
	, SnappedActor(false)
	, CachedLocation(ForceInitToZero)
	, PivotLocation(ForceInitToZero)
	, SnappedLocation(ForceInitToZero)
	, GridBase(ForceInitToZero)
	, TranslateRotateXAxisAngle(0.0f)
	, TranslateRotate2DAngle(0.0f)
	, DefaultModeIDs()
	, WidgetMode(FWidget::WM_None)
	, OverrideWidgetMode(FWidget::WM_None)
	, bShowWidget(true)
	, bHideViewportUI(false)
	, bSelectionHasSceneComponent(false)
	, WidgetScale(1.0f)
	, CoordSystem(COORD_World)
	, bIsTracking(false)
{
	DefaultModeIDs.Add( FBuiltinEditorModes::EM_Default );

	// Load the last used settings
	LoadConfig();

	// Register our callback for actor selection changes
	USelection::SelectNoneEvent.AddRaw(this, &FEditorModeTools::OnEditorSelectNone);
	USelection::SelectionChangedEvent.AddRaw(this, &FEditorModeTools::OnEditorSelectionChanged);
	USelection::SelectObjectEvent.AddRaw(this, &FEditorModeTools::OnEditorSelectionChanged);

	if( GEditor )
	{
		// Register our callback for undo/redo
		GEditor->RegisterForUndo(this);

		// This binding ensures the mode is destroyed if the type is unregistered outside of normal shutdown process
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModeUnregistered().AddRaw(this, &FEditorModeTools::OnModeUnregistered);
	}
}

FEditorModeTools::~FEditorModeTools()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModeUnregistered().RemoveAll(this);
	}

	DeactivateAllModes();
	RecycledScriptableModes.Empty();

	USelection::SelectionChangedEvent.RemoveAll(this);
	USelection::SelectNoneEvent.RemoveAll(this);
	USelection::SelectObjectEvent.RemoveAll(this);
}

void FEditorModeTools::LoadConfig(void)
{
	GConfig->GetBool(TEXT("FEditorModeTools"),TEXT("ShowWidget"),bShowWidget,
		GEditorPerProjectIni);

	const bool bGetRawValue = true;
	int32 Bogus = (int32)GetCoordSystem(bGetRawValue);
	GConfig->GetInt(TEXT("FEditorModeTools"),TEXT("CoordSystem"),Bogus,
		GEditorPerProjectIni);
	SetCoordSystem((ECoordSystem)Bogus);


	LoadWidgetSettings();
}

void FEditorModeTools::SaveConfig(void)
{
	GConfig->SetBool(TEXT("FEditorModeTools"), TEXT("ShowWidget"), bShowWidget, GEditorPerProjectIni);

	const bool bGetRawValue = true;
	GConfig->SetInt(TEXT("FEditorModeTools"), TEXT("CoordSystem"), (int32)GetCoordSystem(bGetRawValue), GEditorPerProjectIni);

	SaveWidgetSettings();
}

TSharedPtr<class IToolkitHost> FEditorModeTools::GetToolkitHost() const
{
	TSharedPtr<class IToolkitHost> Result = ToolkitHost.Pin();
	check(ToolkitHost.IsValid());
	return Result;
}

bool FEditorModeTools::HasToolkitHost() const
{
	return ToolkitHost.Pin().IsValid();
}

void FEditorModeTools::SetToolkitHost(TSharedRef<class IToolkitHost> InHost)
{
	checkf(!ToolkitHost.IsValid(), TEXT("SetToolkitHost can only be called once"));
	ToolkitHost = InHost;
}

USelection* FEditorModeTools::GetSelectedActors() const
{
	return GEditor->GetSelectedActors();
}

USelection* FEditorModeTools::GetSelectedObjects() const
{
	return GEditor->GetSelectedObjects();
}

USelection* FEditorModeTools::GetSelectedComponents() const
{
	return GEditor->GetSelectedComponents();
}

UWorld* FEditorModeTools::GetWorld() const
{
	// When in 'Simulate' mode, the editor mode tools will actually interact with the PIE world
	if( GEditor->bIsSimulatingInEditor )
	{
		return GEditor->GetPIEWorldContext()->World();
	}
	else
	{
		return GEditor->GetEditorWorldContext().World();
	}
}

FEditorViewportClient* FEditorModeTools::GetHoveredViewportClient() const
{
	// This is our best effort right now. However this is somewhat incorrect as if you Hover
	// on other Viewports they get mouse events, but this value stays on the Focused viewport.
	// Not sure what to do about this right now.
	return HoveredViewportClient;
}

FEditorViewportClient* FEditorModeTools::GetFocusedViewportClient() const
{
	// This is our best effort right now. However this is somewhat incorrect as if you Hover
	// on other Viewports they get mouse events, but this value stays on the Focused viewport.
	// Not sure what to do about this right now.
	return FocusedViewportClient;
}

bool FEditorModeTools::SelectionHasSceneComponent() const
{
	return bSelectionHasSceneComponent;
}

bool FEditorModeTools::IsSelectionAllowed(AActor* InActor, const bool bInSelected) const
{
	bool bSelectionAllowed = (ActiveScriptableModes.Num() == 0);

	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bSelectionAllowed |= Mode->IsSelectionAllowed(InActor, bInSelected);
	}

	return bSelectionAllowed;
}

bool FEditorModeTools::IsSelectionHandled(AActor* InActor, const bool bInSelected) const
{
	bool bSelectionHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bSelectionHandled |= Mode->Select(InActor, bInSelected);
	}

	return bSelectionHandled;
}

bool FEditorModeTools::ProcessEditDuplicate()
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled = Mode->ProcessEditDuplicate();
	}

	return bHandled;
}

bool FEditorModeTools::ProcessEditDelete()
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled = Mode->ProcessEditDelete();
	}

	return bHandled;
}

bool FEditorModeTools::ProcessEditCut()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->ProcessEditCut())
		{
			return true;
		}
	}

	return false;
}

bool FEditorModeTools::ProcessEditCopy()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->ProcessEditCopy())
		{
			return true;
		}
	}

	return false;
}

bool FEditorModeTools::ProcessEditPaste()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->ProcessEditPaste())
		{
			return true;
		}
	}

	return false;
}

EEditAction::Type FEditorModeTools::GetActionEditDuplicate()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		const EEditAction::Type EditAction = Mode->GetActionEditDuplicate();
		if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
		{
			return EditAction;
		}
	}

	return EEditAction::Skip;
}

EEditAction::Type FEditorModeTools::GetActionEditDelete()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		const EEditAction::Type EditAction = Mode->GetActionEditDelete();
		if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
		{
			return EditAction;
		}
	}

	return EEditAction::Skip;
}

EEditAction::Type FEditorModeTools::GetActionEditCut()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		const EEditAction::Type EditAction = Mode->GetActionEditCut();
		if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
		{
			return EditAction;
		}
	}

	return EEditAction::Skip;
}

EEditAction::Type FEditorModeTools::GetActionEditCopy()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		const EEditAction::Type EditAction = Mode->GetActionEditCopy();
		if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
		{
			return EditAction;
		}
	}

	return EEditAction::Skip;
}

EEditAction::Type FEditorModeTools::GetActionEditPaste()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		const EEditAction::Type EditAction = Mode->GetActionEditPaste();
		if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
		{
			return EditAction;
		}
	}

	return EEditAction::Skip;
}

void FEditorModeTools::DeactivateOtherVisibleModes(FEditorModeID InMode)
{
	TArray<UEdMode*> TempModes(ActiveScriptableModes);
	for (const UEdMode* Mode : TempModes)
	{
		if (Mode->GetID() != InMode && Mode->GetModeInfo().bVisible)
		{
			DeactivateMode(Mode->GetID());
		}
	}
}

bool FEditorModeTools::IsSnapRotationEnabled() const
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->IsSnapRotationEnabled())
		{
			return true;
		}
	}

	return false;
}

bool FEditorModeTools::SnapRotatorToGridOverride(FRotator& InRotation) const
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->SnapRotatorToGridOverride(InRotation))
		{
			return true;
		}
	}

	return false;
}

void FEditorModeTools::ActorsDuplicatedNotify(TArray<AActor*>& InPreDuplicateSelection, TArray<AActor*>& InPostDuplicateSelection, const bool bOffsetLocations)
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		// Tell the tools about the duplication
		Mode->ActorsDuplicatedNotify(InPreDuplicateSelection, InPostDuplicateSelection, bOffsetLocations);
	}
}

void FEditorModeTools::ActorMoveNotify()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		// Also notify the current editing modes if they are interested.
		Mode->ActorMoveNotify();
	}
}

void FEditorModeTools::ActorSelectionChangeNotify()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		Mode->ActorSelectionChangeNotify();
	}
}

void FEditorModeTools::ActorPropChangeNotify()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		Mode->ActorPropChangeNotify();
	}
}

void FEditorModeTools::UpdateInternalData()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		Mode->UpdateInternalData();
	}
}

bool FEditorModeTools::IsOnlyVisibleActiveMode(FEditorModeID InMode) const
{
	// Only return true if this is the *only* active mode
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->GetModeInfo().bVisible && Mode->GetID() != InMode)
		{
			return false;
		}
	}
	return true;
}

void FEditorModeTools::OnEditorSelectionChanged(UObject* NewSelection)
{
	if(NewSelection == GetSelectedActors())
	{
		// when actors are selected check if there is at least one component selected and cache that off
		// Editor modes use this primarily to determine of transform gizmos should be drawn.  
		// Performing this check each frame with lots of actors is expensive so only do this when selection changes
		bSelectionHasSceneComponent = false;
		for(FSelectionIterator It(*GetSelectedActors()); It; ++It)
		{
			AActor* Actor = Cast<AActor>(*It);
			if(Actor != nullptr && Actor->FindComponentByClass<USceneComponent>() != nullptr)
			{
				bSelectionHasSceneComponent = true;
				break;
			}
		}

	}
	else
	{
		// If selecting an actor, move the pivot location.
		AActor* Actor = Cast<AActor>(NewSelection);
		if(Actor != nullptr)
		{
			if(Actor->IsSelected())
			{
				SetPivotLocation(Actor->GetActorLocation(), false);

				// If this actor wasn't part of the original selection set during pie/sie, clear it now
				if(GEditor->ActorsThatWereSelected.Num() > 0)
				{
					AActor* EditorActor = EditorUtilities::GetEditorWorldCounterpartActor(Actor);
					if(!EditorActor || !GEditor->ActorsThatWereSelected.Contains(EditorActor))
					{
						GEditor->ActorsThatWereSelected.Empty();
					}
				}
			}
			else if(GEditor->ActorsThatWereSelected.Num() > 0)
			{
				// Clear the selection set
				GEditor->ActorsThatWereSelected.Empty();
			}
		}
	}

	for(const auto& Pair : FEditorModeRegistry::Get().GetFactoryMap())
	{
		Pair.Value->OnSelectionChanged(*this, NewSelection);
	}
}

void FEditorModeTools::OnEditorSelectNone()
{
	GEditor->SelectNone( false, true );
	GEditor->ActorsThatWereSelected.Empty();
}

void FEditorModeTools::SetPivotLocation( const FVector& Location, const bool bIncGridBase )
{
	CachedLocation = PivotLocation = SnappedLocation = Location;
	if ( bIncGridBase )
	{
		GridBase = Location;
	}
}

ECoordSystem FEditorModeTools::GetCoordSystem(bool bGetRawValue)
{
	bool bAligningToActors = false;
	if (GEditor->GetEditorWorldExtensionsManager() != nullptr
		&& GetWorld() != nullptr)
	{
		UEditorWorldExtensionCollection* WorldExtensionCollection = GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions(GetWorld(), false);
		if (WorldExtensionCollection != nullptr)
		{
			UViewportWorldInteraction* ViewportWorldInteraction = Cast<UViewportWorldInteraction>(WorldExtensionCollection->FindExtension(UViewportWorldInteraction::StaticClass()));
			if (ViewportWorldInteraction != nullptr && ViewportWorldInteraction->AreAligningToActors() == true)
			{
				bAligningToActors = true;
			}
		}
	}
	if (!bGetRawValue && 
		((GetWidgetMode() == FWidget::WM_Scale) || bAligningToActors))
	{
		return COORD_Local;
	}
	else
	{
		return CoordSystem;
	}
}

void FEditorModeTools::SetCoordSystem(ECoordSystem NewCoordSystem)
{
	// If we are trying to enter world space but are aligning to actors, turn off aligning to actors
	if (GEditor->GetEditorWorldExtensionsManager() != nullptr
		&& GetWorld() != nullptr
		&& NewCoordSystem == COORD_World)
	{
		UEditorWorldExtensionCollection* WorldExtensionCollection = GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions(GetWorld(), false);
		if (WorldExtensionCollection != nullptr)
		{
			UViewportWorldInteraction* ViewportWorldInteraction = Cast<UViewportWorldInteraction>(WorldExtensionCollection->FindExtension(UViewportWorldInteraction::StaticClass()));
			if (ViewportWorldInteraction != nullptr && ViewportWorldInteraction->AreAligningToActors() == true)
			{
				if (ViewportWorldInteraction->HasCandidatesSelected())
				{
					ViewportWorldInteraction->SetSelectionAsCandidates();
				}
				GUnrealEd->Exec(GetWorld(), TEXT("VI.EnableGuides 0"));
			}
		}
	}
	CoordSystem = NewCoordSystem;
	BroadcastCoordSystemChanged(NewCoordSystem);
}

void FEditorModeTools::SetDefaultMode( const FEditorModeID DefaultModeID )
{
	DefaultModeIDs.Reset();
	DefaultModeIDs.Add( DefaultModeID );
}

void FEditorModeTools::AddDefaultMode( const FEditorModeID DefaultModeID )
{
	DefaultModeIDs.AddUnique( DefaultModeID );
}

void FEditorModeTools::RemoveDefaultMode( const FEditorModeID DefaultModeID )
{
	DefaultModeIDs.RemoveSingle( DefaultModeID );
}

void FEditorModeTools::ActivateDefaultMode()
{
	// NOTE: Activating EM_Default will cause ALL default editor modes to be activated (handled specially in ActivateMode())
	ActivateMode( FBuiltinEditorModes::EM_Default );
}

void FEditorModeTools::DeactivateScriptableModeAtIndex(int32 InIndex)
{
	check(InIndex >= 0 && InIndex < ActiveScriptableModes.Num());

	UEdMode* Mode = ActiveScriptableModes[InIndex];

	Mode->Exit();

	const bool bIsEnteringMode = false;
	BroadcastEditorModeIDChanged(Mode->GetID(), bIsEnteringMode);

	// Remove the toolbar widget
	ActiveToolBarRows.RemoveAll(
		[&Mode](FEdModeToolbarRow& Row)
		{
			return Row.ModeID == Mode->GetID();
		}
	);

	RebuildModeToolBar();

	RecycledScriptableModes.Add(Mode->GetID(), Mode);
	ActiveScriptableModes.RemoveAt(InIndex);
}

void FEditorModeTools::OnModeUnregistered(FEditorModeID ModeID)
{
	DestroyMode(ModeID);
}

void FEditorModeTools::RebuildModeToolBar()
{
	// If the tab or box is not valid the toolbar has not been opened or has been closed by the user
	TSharedPtr<SVerticalBox> ModeToolbarBoxPinned = ModeToolbarBox.Pin();
	if (ModeToolbarTab.IsValid() && ModeToolbarBoxPinned.IsValid())
	{
		ModeToolbarBoxPinned->ClearChildren();

		TSharedRef< SHorizontalBox > PaletteTabBox = SNew(SHorizontalBox);
		TSharedRef< SWidgetSwitcher > PaletteSwitcher = SNew(SWidgetSwitcher);

		int32 PaletteCount = ActiveToolBarRows.Num();
		if(PaletteCount > 0)
		{
			for(int32 RowIdx = 0; RowIdx < PaletteCount; ++RowIdx)
			{
				const FEdModeToolbarRow& Row = ActiveToolBarRows[RowIdx];
				if (ensure(Row.ToolbarWidget.IsValid()))
				{
					TSharedRef<SWidget> PaletteWidget = Row.ToolbarWidget.ToSharedRef();

					
					TSharedPtr<FModeToolkit> RowToolkit;
					if (FEdMode* Mode = GetActiveMode(Row.ModeID))
					{
						RowToolkit = Mode->GetToolkit();
					}
					else if (UEdMode* ScriptableMode = GetActiveScriptableMode(Row.ModeID))
					{
						RowToolkit = ScriptableMode->GetToolkit();
					}

					// Don't show Palette Tabs if there is only one
					if (PaletteCount > 1)
					{
						PaletteTabBox->AddSlot()
						.AutoWidth()
						.Padding(FMargin(0.f, 1.0f, 1.0f, 0.0f))
						[
							SNew(SCheckBox)
							.Style( FEditorStyle::Get(),  "ToolPalette.DockingTab" )
							.OnCheckStateChanged_Lambda( [PaletteSwitcher, Row, RowToolkit] (const ECheckBoxState) { 
									PaletteSwitcher->SetActiveWidget(Row.ToolbarWidget.ToSharedRef());
									RowToolkit->OnToolPaletteChanged(Row.PaletteName);
								} 
							)
							.IsChecked_Lambda( [PaletteSwitcher, PaletteWidget] () -> ECheckBoxState { return PaletteSwitcher->GetActiveWidget() == PaletteWidget ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							[
								SNew(STextBlock)
								.Text(Row.DisplayName)
							]
						];
					}

					PaletteSwitcher->AddSlot()
					[
						PaletteWidget
					]; 
				}
			}

			ModeToolbarBoxPinned->AddSlot()
			.AutoHeight()
			[
				SNew(SOverlay)
				+SOverlay::Slot()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("ToolPalette.DockingWell"))
				]

				+SOverlay::Slot()
				[
					PaletteTabBox
				]
			];

			ModeToolbarBoxPinned->AddSlot()
			.Padding(1.f)
			[
				SNew(SBox)
				.HeightOverride(45.f)
				[
					PaletteSwitcher 
				]
			];

			ModeToolbarPaletteSwitcher = PaletteSwitcher;
		}
		else
		{
			ModeToolbarTab.Pin()->RequestCloseTab();
		}
	}
}

void FEditorModeTools::SpawnOrUpdateModeToolbar()
{
	if(ShouldShowModeToolbar())
	{
		if (ModeToolbarTab.IsValid())
		{
			RebuildModeToolBar();
		}
		else if (ToolkitHost.IsValid())
		{
			ToolkitHost.Pin()->GetTabManager()->TryInvokeTab(EditorModeToolbarTabName);
		}
	}
}

void FEditorModeTools::InvokeToolPaletteTab(FEditorModeID InModeID, FName InPaletteName)
{
	if (!ModeToolbarPaletteSwitcher.Pin()) 
	{
		return;
	}

	for (auto Row: ActiveToolBarRows)
	{
		if (Row.ModeID == InModeID && Row.PaletteName == InPaletteName)
		{
			TSharedRef<SWidget> PaletteWidget = Row.ToolbarWidget.ToSharedRef();

			TSharedPtr<FModeToolkit> RowToolkit;
			if (FEdMode* Mode = GetActiveMode(InModeID))
			{
				RowToolkit = Mode->GetToolkit();
			}
			else if (UEdMode* ScriptableMode = GetActiveScriptableMode(InModeID))
			{
				RowToolkit = ScriptableMode->GetToolkit();
			}

			TSharedPtr<SWidget> ActiveWidget = ModeToolbarPaletteSwitcher.Pin()->GetActiveWidget();
			if (RowToolkit && ActiveWidget.Get() != Row.ToolbarWidget.Get())
			{
				ModeToolbarPaletteSwitcher.Pin()->SetActiveWidget(Row.ToolbarWidget.ToSharedRef());
				RowToolkit->OnToolPaletteChanged(Row.PaletteName);
			}
			break;	
		}
	}	
}

void FEditorModeTools::DeactivateMode( FEditorModeID InID )
{
	// Find the mode from the ID and exit it.
	for (int32 Index = ActiveScriptableModes.Num() - 1; Index >= 0; --Index)
	{
		auto& Mode = ActiveScriptableModes[Index];
		if (Mode->GetID() == InID)
		{
			DeactivateScriptableModeAtIndex(Index);
			break;
		}
	}


	if ((ActiveScriptableModes.Num() == 0))
	{
		// Ensure the default mode is active if there are no active modes.
		ActivateDefaultMode();
	}
}

void FEditorModeTools::DeactivateAllModes()
{
	for (int32 Index = ActiveScriptableModes.Num() - 1; Index >= 0; --Index)
	{
		DeactivateScriptableModeAtIndex(Index);
	}
}

void FEditorModeTools::DestroyMode( FEditorModeID InID )
{
	// Since deactivating the last active mode will cause the default modes to be activated, make sure this mode is removed from defaults.
	RemoveDefaultMode( InID );
	
	// Add back the default default mode if we just removed the last valid default.
	if ( DefaultModeIDs.Num() == 0 )
	{
		AddDefaultMode( FBuiltinEditorModes::EM_Default );
	}

	// Find the mode from the ID and exit it.
	for (int32 Index = ActiveScriptableModes.Num() - 1; Index >= 0; --Index)
	{
		auto& Mode = ActiveScriptableModes[Index];
		if (Mode->GetID() == InID)
		{
			// Deactivate and destroy
			DeactivateScriptableModeAtIndex(Index);
			break;
		}
	}

	RecycledScriptableModes.Remove(InID);
}

TSharedRef<SDockTab> FEditorModeTools::MakeModeToolbarTab()
{
	TSharedRef<SDockTab> ToolbarTabRef = 	
		SNew(SDockTab)
		.Label(NSLOCTEXT("EditorModes", "EditorModesToolbarTitle", "Mode Toolbar"))
		.ShouldAutosize(true)
		.ContentPadding(0.0f)
		.Icon(FEditorStyle::GetBrush("ToolBar.Icon"))
		[
			SAssignNew(ModeToolbarBox, SVerticalBox)	
		];

	ModeToolbarTab = ToolbarTabRef;

	// Rebuild the toolbar with existing mode tools that may be active
	RebuildModeToolBar();

	return ToolbarTabRef;

}

bool FEditorModeTools::ShouldShowModeToolbar() const
{
	return ActiveToolBarRows.Num() > 0;
}

bool FEditorModeTools::ShouldShowModeToolbox() const
{
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->GetModeInfo().bVisible && Mode->UsesToolkits())
		{
			return true;
		}
	}

	return false;
}

void FEditorModeTools::ActivateMode(FEditorModeID InID, bool bToggle)
{
	static bool bReentrant = false;
	if( !bReentrant )
	{
		if (InID == FBuiltinEditorModes::EM_Default)
		{
			bReentrant = true;

			for( const FEditorModeID& ModeID : DefaultModeIDs )
			{
				ActivateMode( ModeID );
			}

			for( const FEditorModeID& ModeID : DefaultModeIDs )
			{
				check( IsModeActive( ModeID ) );
			}

			bReentrant = false;
			return;
		}
	}

	// Check to see if the mode is already active
	if (IsModeActive(InID))
	{
		// The mode is already active toggle it off if we should toggle off already active modes.
		if (bToggle)
		{
			DeactivateMode(InID);
		}
		// Nothing more to do
		return;
	}

	// Recycle a mode or factory a new one
	UEdMode* ScriptableMode = RecycledScriptableModes.FindRef(InID);
	if (ScriptableMode)
	{
		RecycledScriptableModes.Remove(InID);
	}
	else
	{
		ScriptableMode = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CreateEditorModeWithToolsOwner(InID, *this);
	}

	if (!ScriptableMode)
	{
		UE_LOG(LogEditorModes, Log, TEXT("FEditorModeTools::ActivateMode : Couldn't find mode '%s'."), *InID.ToString());
		// Just return and leave the mode list unmodified
		return;
	}

	// Remove anything that isn't compatible with this mode
	for (int32 ModeIndex = ActiveScriptableModes.Num() - 1; ModeIndex >= 0; ModeIndex--)
	{
		const bool bModesAreCompatible = ScriptableMode->IsCompatibleWith(ActiveScriptableModes[ModeIndex]->GetID()) || ActiveScriptableModes[ModeIndex]->IsCompatibleWith(ScriptableMode->GetID());
		if (!bModesAreCompatible)
		{
			DeactivateScriptableModeAtIndex(ModeIndex);
		}
	}

	ActiveScriptableModes.Add(ScriptableMode);
	// Enter the new mode
	ScriptableMode->Enter();

	const bool bIsEnteringMode = true;
	BroadcastEditorModeIDChanged(ScriptableMode->GetID(), bIsEnteringMode);

	// Ask the mode to build the toolbar.
	TSharedPtr<FUICommandList> CommandList;
	const TSharedPtr<FModeToolkit> Toolkit = ScriptableMode->GetToolkit();
	if (Toolkit.IsValid())
	{
		CommandList = Toolkit->GetToolkitCommands();

		// Also build the toolkit here 
		int32 PaletteCount = 0;
		TArray<FName> PaletteNames;
		Toolkit->GetToolPaletteNames(PaletteNames);
		for (auto Palette : PaletteNames)
		{
			FUniformToolBarBuilder ModeToolbarBuilder(CommandList, FMultiBoxCustomization(ScriptableMode->GetModeInfo().ToolbarCustomizationName), TSharedPtr<FExtender>(), false);
			ModeToolbarBuilder.SetStyle(&FEditorStyle::Get(), "PaletteToolBar");
			Toolkit->BuildToolPalette(Palette, ModeToolbarBuilder);

			ActiveToolBarRows.Emplace(ScriptableMode->GetID(), Palette, Toolkit->GetToolPaletteDisplayName(Palette), ModeToolbarBuilder.MakeWidget());
			PaletteCount++;
		}

		if (PaletteCount > 0)
		{
			SpawnOrUpdateModeToolbar();
		}
	}
	
	// Update the editor UI
	FEditorSupportDelegates::UpdateUI.Broadcast();
}

bool FEditorModeTools::EnsureNotInMode(FEditorModeID ModeID, const FText& ErrorMsg, bool bNotifyUser) const
{
	// We're in a 'safe' mode if we're not in the specified mode.
	const bool bInASafeMode = !IsModeActive(ModeID);
	if( !bInASafeMode && !ErrorMsg.IsEmpty() )
	{
		// Do we want to display this as a notification or a dialog to the user
		if ( bNotifyUser )
		{
			FNotificationInfo Info( ErrorMsg );
			FSlateNotificationManager::Get().AddNotification( Info );
		}
		else
		{
			FMessageDialog::Open( EAppMsgType::Ok, ErrorMsg );
		}		
	}
	return bInASafeMode;
}

UEdMode* FEditorModeTools::GetActiveScriptableMode(FEditorModeID InID) const
{
	for (auto& Mode : ActiveScriptableModes)
	{
		if (Mode->GetID() == InID)
		{
			return Mode;
		}
	}

	return nullptr;
}

/**
 * Returns a coordinate system that should be applied on top of the worldspace system.
 */

FMatrix FEditorModeTools::GetCustomDrawingCoordinateSystem()
{
	FMatrix Matrix = FMatrix::Identity;

	switch (GetCoordSystem())
	{
		case COORD_Local:
		{
			Matrix = GetLocalCoordinateSystem();
		}
		break;

		case COORD_World:
			break;

		default:
			break;
	}

	return Matrix;
}

FMatrix FEditorModeTools::GetCustomInputCoordinateSystem()
{
	return GetCustomDrawingCoordinateSystem();
}

FMatrix FEditorModeTools::GetLocalCoordinateSystem()
{
	FMatrix Matrix = FMatrix::Identity;
	// Let the current mode have a shot at setting the local coordinate system.
	// If it doesn't want to, create it by looking at the currently selected actors list.

	bool CustomCoordinateSystemProvided = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		FEdMode* LegacyMode = Mode->AsLegacyMode();
		if (LegacyMode && LegacyMode->GetCustomDrawingCoordinateSystem(Matrix, nullptr))
		{
			CustomCoordinateSystemProvided = true;
			break;
		}
	}

	if (!CustomCoordinateSystemProvided)
	{
		if (USceneComponent* SceneComponent = GetSelectedComponents()->GetBottom<USceneComponent>())
		{
			Matrix = FQuatRotationMatrix(SceneComponent->GetComponentQuat());
		}
		else
		{
			const int32 Num = GetSelectedActors()->CountSelections<AActor>();

			// Coordinate system needs to come from the last actor selected
			if (Num > 0)
			{
				Matrix = FQuatRotationMatrix(GetSelectedActors()->GetBottom<AActor>()->GetActorQuat());
			}
		}
	}

	if (!Matrix.Equals(FMatrix::Identity))
	{
		Matrix.RemoveScaling();
	}

	return Matrix;
}

/** Gets the widget axis to be drawn */
EAxisList::Type FEditorModeTools::GetWidgetAxisToDraw( FWidget::EWidgetMode InWidgetMode ) const
{
	EAxisList::Type OutAxis = EAxisList::All;
	for( int Index = ActiveScriptableModes.Num() - 1; Index >= 0 ; Index-- )
	{
		FEdMode* Mode = ActiveScriptableModes[Index]->AsLegacyMode();
		if ( Mode && Mode->ShouldDrawWidget() )
		{
			OutAxis = Mode->GetWidgetAxisToDraw( InWidgetMode );
			break;
		}
	}

	return OutAxis;
}

/** Mouse tracking interface.  Passes tracking messages to all active modes */
bool FEditorModeTools::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bIsTracking = true;
	bool bTransactionHandled = false;

	CachedLocation = PivotLocation;	// Cache the pivot location

	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bTransactionHandled |= Mode->StartTracking(InViewportClient, InViewportClient->Viewport);
	}

	return bTransactionHandled;
}

/** Mouse tracking interface.  Passes tracking messages to all active modes */
bool FEditorModeTools::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bIsTracking = false;
	bool bTransactionHandled = false;

	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bTransactionHandled |= Mode->EndTracking(InViewportClient, InViewportClient->Viewport);
	}

	CachedLocation = PivotLocation;	// Clear the pivot location
	
	return bTransactionHandled;
}

bool FEditorModeTools::AllowsViewportDragTool() const
{
	bool bCanUseDragTool = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (FEdMode* LegacyMode = Mode->AsLegacyMode())
		{
			bCanUseDragTool |= LegacyMode->AllowsViewportDragTool();
		}
	}
	return bCanUseDragTool;
}

/** Notifies all active modes that a map change has occured */
void FEditorModeTools::MapChangeNotify()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		Mode->MapChangeNotify();
	}
}

/** Notifies all active modes to empty their selections */
void FEditorModeTools::SelectNone()
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		Mode->SelectNone();
	}
}

/** Notifies all active modes of box selection attempts */
bool FEditorModeTools::BoxSelect( FBox& InBox, bool InSelect )
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (FEdMode* LegacyMode = Mode->AsLegacyMode())
		{
			bHandled |= LegacyMode->BoxSelect(InBox, InSelect);
		}
	}
	return bHandled;
}

/** Notifies all active modes of frustum selection attempts */
bool FEditorModeTools::FrustumSelect( const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect )
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (FEdMode* LegacyMode = Mode->AsLegacyMode())
		{
			bHandled |= LegacyMode->FrustumSelect(InFrustum, InViewportClient, InSelect);
		}
	}
	return bHandled;
}


/** true if any active mode uses a transform widget */
bool FEditorModeTools::UsesTransformWidget() const
{
	bool bUsesTransformWidget = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (FEdMode* LegacyMode = Mode->AsLegacyMode())
		{
			bUsesTransformWidget |= LegacyMode->UsesTransformWidget();
		}
	}

	return bUsesTransformWidget;
}

/** true if any active mode uses the passed in transform widget */
bool FEditorModeTools::UsesTransformWidget( FWidget::EWidgetMode CheckMode ) const
{
	bool bUsesTransformWidget = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (FEdMode* LegacyMode = Mode->AsLegacyMode())
		{
			bUsesTransformWidget |= LegacyMode->UsesTransformWidget(CheckMode);
		}
	}

	return bUsesTransformWidget;
}

/** Sets the current widget axis */
void FEditorModeTools::SetCurrentWidgetAxis( EAxisList::Type NewAxis )
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (FEdMode* LegacyMode = Mode->AsLegacyMode())
		{
			LegacyMode->SetCurrentWidgetAxis(NewAxis);
		}
	}
}

/** Notifies all active modes of mouse click messages. */
bool FEditorModeTools::HandleClick(FEditorViewportClient* InViewportClient,  HHitProxy *HitProxy, const FViewportClick& Click )
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->HandleClick(InViewportClient, HitProxy, Click);
	}

	return bHandled;
}

bool FEditorModeTools::ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox)
{
	bool bHandled = false;
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->ComputeBoundingBoxForViewportFocus(Actor, PrimitiveComponent, InOutBox);
	}

	return bHandled;
}

/** true if the passed in brush actor should be drawn in wireframe */	
bool FEditorModeTools::ShouldDrawBrushWireframe( AActor* InActor ) const
{
	bool bShouldDraw = false;

	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bShouldDraw |= Mode->ShouldDrawBrushWireframe(InActor);
	}

	if((ActiveScriptableModes.Num() == 0))
	{
		// We can get into a state where there are no active modes at editor startup if the builder brush is created before the default mode is activated.
		// Ensure we can see the builder brush when no modes are active.
		bShouldDraw = true;
	}
	return bShouldDraw;
}

/** true if brush vertices should be drawn */
bool FEditorModeTools::ShouldDrawBrushVertices() const
{
	if(UBrushEditingSubsystem* BrushSubsystem = GEditor->GetEditorSubsystem<UBrushEditingSubsystem>())
	{
		// Currently only geometry mode being active prevents vertices from being drawn.
		return !BrushSubsystem->IsGeometryEditorModeActive();
	}

	return true;
}

/** Ticks all active modes */
void FEditorModeTools::Tick( FEditorViewportClient* ViewportClient, float DeltaTime )
{	
	// Remove anything pending destruction
	for (int32 Index = ActiveScriptableModes.Num() - 1; Index >= 0; --Index)
	{
		if (ActiveScriptableModes[Index]->IsPendingDeletion())
		{
			DeactivateScriptableModeAtIndex(Index);
		}
	}


	if (ActiveScriptableModes.Num() == 0)
	{
		// Ensure the default mode is active if there are no active modes.
		ActivateDefaultMode();
	}

	for (UEdMode* Mode : ActiveScriptableModes)
	{
		Mode->Tick(ViewportClient, DeltaTime);
	}
}

/** Notifies all active modes of any change in mouse movement */
bool FEditorModeTools::InputDelta( FEditorViewportClient* InViewportClient,FViewport* InViewport,FVector& InDrag,FRotator& InRot,FVector& InScale )
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
	}
	return bHandled;
}

/** Notifies all active modes of captured mouse movement */	
bool FEditorModeTools::CapturedMouseMove( FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY )
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->CapturedMouseMove(InViewportClient, InViewport, InMouseX, InMouseY);
	}
	return bHandled;
}

/** Notifies all active modes of all captured mouse movement */	
bool FEditorModeTools::ProcessCapturedMouseMoves( FEditorViewportClient* InViewportClient, FViewport* InViewport, const TArrayView<FIntPoint>& CapturedMouseMoves )
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->ProcessCapturedMouseMoves(InViewportClient, InViewport, CapturedMouseMoves);
	}
	return bHandled;
}

/** Notifies all active modes of keyboard input */
bool FEditorModeTools::InputKey(FEditorViewportClient* InViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	bool bHandled = false;
	//Copy the modes and iterate of that since a key may remove the edit mode and change CopyActiveScriptableModes
	TArray<UEdMode*> CopyActiveScriptableModes(ActiveScriptableModes);
	for (UEdMode* Mode : CopyActiveScriptableModes)
	{
		bHandled |= Mode->InputKey(InViewportClient, Viewport, Key, Event);
	}
	return bHandled;
}

/** Notifies all active modes of axis movement */
bool FEditorModeTools::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->InputAxis(InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
	}
	return bHandled;
}

bool FEditorModeTools::GetPivotForOrbit( FVector& Pivot ) const
{
	// Just return the first pivot point specified by a mode
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->GetPivotForOrbit(Pivot))
		{
			return true;
		}
	}
	return false;
}

bool FEditorModeTools::MouseEnter( FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 X, int32 Y )
{
	HoveredViewportClient = InViewportClient;
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->MouseEnter(InViewportClient, Viewport, X, Y);
	}
	return bHandled;
}

bool FEditorModeTools::MouseLeave( FEditorViewportClient* InViewportClient, FViewport* Viewport )
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->MouseLeave(InViewportClient, Viewport);
	}
	return bHandled;
}

/** Notifies all active modes that the mouse has moved */
bool FEditorModeTools::MouseMove( FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 X, int32 Y )
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->MouseMove(InViewportClient, Viewport, X, Y);
	}
	return bHandled;
}

bool FEditorModeTools::ReceivedFocus( FEditorViewportClient* InViewportClient, FViewport* Viewport )
{
	FocusedViewportClient = InViewportClient;
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->ReceivedFocus(InViewportClient, Viewport);
	}
	return bHandled;
}

bool FEditorModeTools::LostFocus( FEditorViewportClient* InViewportClient, FViewport* Viewport )
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->LostFocus(InViewportClient, Viewport);
	}
	return bHandled;
}

/** Draws all active mode components */	
void FEditorModeTools::DrawActiveModes( const FSceneView* InView, FPrimitiveDrawInterface* PDI )
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		Mode->Draw(InView, PDI);
	}
}

/** Renders all active modes */
void FEditorModeTools::Render( const FSceneView* InView, FViewport* Viewport, FPrimitiveDrawInterface* PDI )
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		Mode->Render(InView, Viewport, PDI);
	}
}

/** Draws the HUD for all active modes */
void FEditorModeTools::DrawHUD( FEditorViewportClient* InViewportClient,FViewport* Viewport, const FSceneView* View, FCanvas* Canvas )
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		Mode->DrawHUD(InViewportClient, Viewport, View, Canvas);
	}
}

/** Calls PostUndo on all active modes */
void FEditorModeTools::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		for (UEdMode* Mode : ActiveScriptableModes)
		{
			Mode->PostUndo();
		}
	}
}
void FEditorModeTools::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

/** true if we should allow widget move */
bool FEditorModeTools::AllowWidgetMove() const
{
	bool bAllow = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (FEdMode* LegacyMode = Mode->AsLegacyMode())
		{
			bAllow |= LegacyMode->AllowWidgetMove();
		}
	}
	return bAllow;
}

bool FEditorModeTools::DisallowMouseDeltaTracking() const
{
	bool bDisallow = false;
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bDisallow |= Mode->DisallowMouseDeltaTracking();
	}
	return bDisallow;
}

bool FEditorModeTools::GetCursor(EMouseCursor::Type& OutCursor) const
{
	bool bHandled = false;
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->GetCursor(OutCursor);
	}
	return bHandled;
}

bool FEditorModeTools::GetOverrideCursorVisibility(bool& bWantsOverride, bool& bHardwareCursorVisible, bool bSoftwareCursorVisible) const
{
	bool bHandled = false;
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->GetOverrideCursorVisibility(bWantsOverride, bHardwareCursorVisible, bSoftwareCursorVisible);
	}
	return bHandled;
}

bool FEditorModeTools::PreConvertMouseMovement(FEditorViewportClient* InViewportClient)
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->PreConvertMouseMovement(InViewportClient);
	}
	return bHandled;
}

bool FEditorModeTools::PostConvertMouseMovement(FEditorViewportClient* InViewportClient)
{
	bool bHandled = false;
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->PostConvertMouseMovement(InViewportClient);
	}
	return bHandled;
}

bool FEditorModeTools::GetShowWidget() const
{
	bool bDrawModeSupportsWidgetDrawing = false;
	// Check to see of any active modes support widget drawing
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (FEdMode* LegacyMode = Mode->AsLegacyMode())
		{
			bDrawModeSupportsWidgetDrawing |= LegacyMode->ShouldDrawWidget();
		}
	}
	return bDrawModeSupportsWidgetDrawing && bShowWidget;
}

/**
 * Used to cycle widget modes
 */
void FEditorModeTools::CycleWidgetMode (void)
{
	//make sure we're not currently tracking mouse movement.  If we are, changing modes could cause a crash due to referencing an axis/plane that is incompatible with the widget
	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (ViewportClient->IsTracking())
		{
			return;
		}
	}

	//only cycle when the mode is requesting the drawing of a widget
	if( GetShowWidget() )
	{
		const int32 CurrentWk = GetWidgetMode();
		int32 Wk = CurrentWk;
		do
		{
			Wk++;
			if ((Wk == FWidget::WM_TranslateRotateZ) && (!GetDefault<ULevelEditorViewportSettings>()->bAllowTranslateRotateZWidget))
			{
				Wk++;
			}
			// Roll back to the start if we go past FWidget::WM_Scale
			if( Wk >= FWidget::WM_Max)
			{
				Wk -= FWidget::WM_Max;
			}
		}
		while (!UsesTransformWidget((FWidget::EWidgetMode)Wk) && Wk != CurrentWk);
		SetWidgetMode( (FWidget::EWidgetMode)Wk );
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}
}

/**Save Widget Settings to Ini file*/
void FEditorModeTools::SaveWidgetSettings(void)
{
	GetMutableDefault<UEditorPerProjectUserSettings>()->SaveConfig();
}

/**Load Widget Settings from Ini file*/
void FEditorModeTools::LoadWidgetSettings(void)
{
}

/**
 * Returns a good location to draw the widget at.
 */

FVector FEditorModeTools::GetWidgetLocation() const
{
	for (int Index = ActiveScriptableModes.Num() - 1; Index >= 0; Index--)
	{
		if (FEdMode* LegacyMode = ActiveScriptableModes[Index]->AsLegacyMode())
		{
			if (LegacyMode->UsesTransformWidget())
			{
				return LegacyMode->GetWidgetLocation();
			}
		}
	}
	
	return FVector(ForceInitToZero);
}

/**
 * Changes the current widget mode.
 */

void FEditorModeTools::SetWidgetMode( FWidget::EWidgetMode InWidgetMode )
{
	WidgetMode = InWidgetMode;
}

/**
 * Allows you to temporarily override the widget mode.  Call this function again
 * with FWidget::WM_None to turn off the override.
 */

void FEditorModeTools::SetWidgetModeOverride( FWidget::EWidgetMode InWidgetMode )
{
	OverrideWidgetMode = InWidgetMode;
}

/**
 * Retrieves the current widget mode, taking overrides into account.
 */

FWidget::EWidgetMode FEditorModeTools::GetWidgetMode() const
{
	if( OverrideWidgetMode != FWidget::WM_None )
	{
		return OverrideWidgetMode;
	}

	return WidgetMode;
}

/**
* Set Scale On The Widget
*/

void FEditorModeTools::SetWidgetScale(float InScale)
{
	WidgetScale = InScale;
}

/**
* Get Scale On The Widget
*/

float FEditorModeTools::GetWidgetScale() const
{
	return WidgetScale;
}

bool FEditorModeTools::GetShowFriendlyVariableNames()
{
	return GetDefault<UEditorStyleSettings>()->bShowFriendlyNames;
}

const uint32 FEditorModeTools::GetMaxNumberOfBookmarks(FEditorViewportClient* InViewportClient)
{
	return IBookmarkTypeTools::Get().GetMaxNumberOfBookmarks(InViewportClient);
}

void FEditorModeTools::CompactBookmarks(FEditorViewportClient* InViewportClient)
{
	IBookmarkTypeTools::Get().CompactBookmarks(InViewportClient);
}

/**
 * Sets a bookmark in the levelinfo file, allocating it if necessary.
 */
void FEditorModeTools::SetBookmark( uint32 InIndex, FEditorViewportClient* InViewportClient )
{
	IBookmarkTypeTools::Get().CreateOrSetBookmark(InIndex, InViewportClient);
}

/**
 * Checks to see if a bookmark exists at a given index
 */

bool FEditorModeTools::CheckBookmark( uint32 InIndex, FEditorViewportClient* InViewportClient )
{
	return IBookmarkTypeTools::Get().CheckBookmark(InIndex, InViewportClient);
}

/**
 * Retrieves a bookmark from the list.
 */
void FEditorModeTools::JumpToBookmark(uint32 InIndex, TSharedPtr<FBookmarkBaseJumpToSettings> InSettings, FEditorViewportClient* InViewportClient)
{
	IBookmarkTypeTools::Get().JumpToBookmark(InIndex, InSettings, InViewportClient);
}

/**
 * Clears a bookmark
 */
void FEditorModeTools::ClearBookmark( uint32 InIndex, FEditorViewportClient* InViewportClient )
{
	IBookmarkTypeTools::Get().ClearBookmark(InIndex, InViewportClient);
}

/**
* Clears all book marks
*/
void FEditorModeTools::ClearAllBookmarks( FEditorViewportClient* InViewportClient )
{
	IBookmarkTypeTools::Get().ClearAllBookmarks(InViewportClient);
}

void FEditorModeTools::AddReferencedObjects( FReferenceCollector& Collector )
{
	Collector.AddReferencedObjects(ActiveScriptableModes);
	Collector.AddReferencedObjects(RecycledScriptableModes);
}

FEdMode* FEditorModeTools::GetActiveMode( FEditorModeID InID )
{
	if (UEdMode* Mode = GetActiveScriptableMode(InID))
	{
		return Mode->AsLegacyMode();
	}

	return nullptr;
}

const FEdMode* FEditorModeTools::GetActiveMode( FEditorModeID InID ) const
{
	if (UEdMode* Mode = GetActiveScriptableMode(InID))
	{
		return Mode->AsLegacyMode();
	}

	return nullptr;
}

const FModeTool* FEditorModeTools::GetActiveTool( FEditorModeID InID ) const
{
	const FEdMode* ActiveMode = GetActiveMode( InID );
	const FModeTool* Tool = nullptr;
	if( ActiveMode )
	{
		Tool = ActiveMode->GetCurrentTool();
	}
	return Tool;
}

bool FEditorModeTools::IsModeActive( FEditorModeID InID ) const
{
	if (GetActiveMode(InID) != nullptr)
	{
		return true;
	}
	else if (GetActiveScriptableMode(InID) != nullptr)
	{
		return true;
	}
	return false;
}

bool FEditorModeTools::IsDefaultModeActive() const
{
	bool bAllDefaultModesActive = true;
	for( const FEditorModeID& ModeID : DefaultModeIDs )
	{
		if( !IsModeActive( ModeID ) )
		{
			bAllDefaultModesActive = false;
			break;
		}
	}
	return bAllDefaultModesActive;
}

bool FEditorModeTools::CanCycleWidgetMode() const
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (FEdMode* LegacyMode = Mode->AsLegacyMode())
		{
			if (LegacyMode->CanCycleWidgetMode())
			{
				return true;
			}
		}
	}
	return false;
}


bool FEditorModeTools::CanAutoSave() const
{
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->CanAutoSave() == false)
		{
			return false;
		}
	}

	return true;
}
