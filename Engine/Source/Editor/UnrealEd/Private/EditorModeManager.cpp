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
#include "Subsystems/BrushEditingSubsystem.h"
#include "Tools/UEdMode.h"
#include "Widgets/Images/SImage.h"

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
	}
}

FEditorModeTools::~FEditorModeTools()
{
	// Should we call Exit on any modes that are still active, or is it too late?
	USelection::SelectionChangedEvent.RemoveAll(this);
	USelection::SelectNoneEvent.RemoveAll(this);
	USelection::SelectObjectEvent.RemoveAll(this);

	GEditor->UnregisterForUndo(this);
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

bool FEditorModeTools::SelectionHasSceneComponent() const
{
	return bSelectionHasSceneComponent;
}

bool FEditorModeTools::IsSelectionAllowed(AActor* InActor, const bool bInSelected) const
{
	bool bSelectionAllowed = ((ActiveModes.Num() == 0) && (ActiveScriptableModes.Num() == 0));
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		bSelectionAllowed |= ActiveModes[ModeIndex]->IsSelectionAllowed(InActor, bInSelected);
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		bSelectionAllowed |= ActiveScriptableModes[ModeIndex]->IsSelectionAllowed(InActor, bInSelected);
	}

	return bSelectionAllowed;
}

bool FEditorModeTools::IsSelectionHandled(AActor* InActor, const bool bInSelected) const
{
	bool bSelectionHandled = false;
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		bSelectionHandled |= ActiveModes[ModeIndex]->Select(InActor, bInSelected);
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		bSelectionHandled |= ActiveScriptableModes[ModeIndex]->Select(InActor, bInSelected);
	}

	return bSelectionHandled;
}

bool FEditorModeTools::ProcessEditDuplicate()
{
	bool bHandled = false;
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		bHandled |= ActiveModes[ModeIndex]->ProcessEditDuplicate();
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		bHandled |= ActiveScriptableModes[ModeIndex]->ProcessEditDuplicate();
	}
	return bHandled;
}

bool FEditorModeTools::ProcessEditDelete()
{
	bool bHandled = false;
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		bHandled |= ActiveModes[ModeIndex]->ProcessEditDelete();
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		bHandled |= ActiveScriptableModes[ModeIndex]->ProcessEditDelete();
	}
	return bHandled;
}

bool FEditorModeTools::ProcessEditCut()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		if (ActiveModes[ModeIndex]->ProcessEditCut())
		{
			return true;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		if (ActiveScriptableModes[ModeIndex]->ProcessEditCut())
		{
			return true;
		}
	}
	return false;
}

bool FEditorModeTools::ProcessEditCopy()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		if (ActiveModes[ModeIndex]->ProcessEditCopy())
		{
			return true;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		if (ActiveScriptableModes[ModeIndex]->ProcessEditCopy())
		{
			return true;
		}
	}
	return false;
}

bool FEditorModeTools::ProcessEditPaste()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		if (ActiveModes[ModeIndex]->ProcessEditPaste())
		{
			return true;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		if (ActiveScriptableModes[ModeIndex]->ProcessEditPaste())
		{
			return true;
		}
	}
	return false;
}

EEditAction::Type FEditorModeTools::GetActionEditDuplicate()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveModes[ModeIndex]->GetActionEditDuplicate();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveScriptableModes[ModeIndex]->GetActionEditDuplicate();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	return EEditAction::Skip;
}

EEditAction::Type FEditorModeTools::GetActionEditDelete()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveModes[ModeIndex]->GetActionEditDelete();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveScriptableModes[ModeIndex]->GetActionEditDelete();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	return EEditAction::Skip;
}

EEditAction::Type FEditorModeTools::GetActionEditCut()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveModes[ModeIndex]->GetActionEditCut();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveScriptableModes[ModeIndex]->GetActionEditCut();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	return EEditAction::Skip;
}

EEditAction::Type FEditorModeTools::GetActionEditCopy()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveModes[ModeIndex]->GetActionEditCopy();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveScriptableModes[ModeIndex]->GetActionEditCopy();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	return EEditAction::Skip;
}

EEditAction::Type FEditorModeTools::GetActionEditPaste()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveModes[ModeIndex]->GetActionEditPaste();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		const EEditAction::Type CanProcess = ActiveScriptableModes[ModeIndex]->GetActionEditPaste();
		if (CanProcess == EEditAction::Process || CanProcess == EEditAction::Halt)
		{
			return CanProcess;
		}
	}
	return EEditAction::Skip;
}

void FEditorModeTools::DeactivateOtherVisibleModes(FEditorModeID InMode)
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ModeIndex++)
	{
		if (ActiveModes[ModeIndex]->GetID() != InMode && ActiveModes[ModeIndex]->GetModeInfo().bVisible)
		{
			DeactivateMode(ActiveModes[ModeIndex]->GetID());
		}
	}

	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ModeIndex++)
	{
		if (ActiveScriptableModes[ModeIndex]->GetID() != InMode && ActiveScriptableModes[ModeIndex]->GetModeInfo().bVisible)
		{
			DeactivateMode(ActiveScriptableModes[ModeIndex]->GetID());
		}
	}
}

bool FEditorModeTools::IsSnapRotationEnabled() const
{
	bool bSnapRotationEnabled = false;
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		bSnapRotationEnabled |= ActiveModes[ModeIndex]->IsSnapRotationEnabled();
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		bSnapRotationEnabled |= ActiveScriptableModes[ModeIndex]->IsSnapRotationEnabled();
	}
	return bSnapRotationEnabled;
}

bool FEditorModeTools::SnapRotatorToGridOverride(FRotator& InRotation) const
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		if (ActiveModes[ModeIndex]->SnapRotatorToGridOverride(InRotation))
		{
			return true;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		if (ActiveScriptableModes[ModeIndex]->SnapRotatorToGridOverride(InRotation))
		{
			return true;
		}
	}
	return false;
}

void FEditorModeTools::ActorsDuplicatedNotify(TArray<AActor*>& InPreDuplicateSelection, TArray<AActor*>& InPostDuplicateSelection, const bool bOffsetLocations)
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		// Tell the tools about the duplication
		ActiveModes[ModeIndex]->ActorsDuplicatedNotify(InPreDuplicateSelection, InPostDuplicateSelection, bOffsetLocations);
	}

	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		// Tell the tools about the duplication
		ActiveScriptableModes[ModeIndex]->ActorsDuplicatedNotify(InPreDuplicateSelection, InPostDuplicateSelection, bOffsetLocations);
	}
}

void FEditorModeTools::ActorMoveNotify()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		// Also notify the current editing modes if they are interested.
		ActiveModes[ModeIndex]->ActorMoveNotify();
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		// Also notify the current editing modes if they are interested.
		ActiveScriptableModes[ModeIndex]->ActorMoveNotify();
	}
}

void FEditorModeTools::ActorSelectionChangeNotify()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		ActiveModes[ModeIndex]->ActorSelectionChangeNotify();
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		ActiveScriptableModes[ModeIndex]->ActorSelectionChangeNotify();
	}
}

void FEditorModeTools::ActorPropChangeNotify()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		ActiveModes[ModeIndex]->ActorPropChangeNotify();
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		ActiveScriptableModes[ModeIndex]->ActorPropChangeNotify();
	}
}

void FEditorModeTools::UpdateInternalData()
{
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		ActiveModes[ModeIndex]->UpdateInternalData();
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		ActiveScriptableModes[ModeIndex]->UpdateInternalData();
	}
}

bool FEditorModeTools::IsOnlyVisibleActiveMode(FEditorModeID InMode) const
{
	// Only return true if this is the *only* active mode
	for (TSharedPtr<FEdMode> Mode : ActiveModes)
	{
		if (Mode->GetModeInfo().bVisible && Mode->GetID() != InMode)
		{
			return false;
		}
	}
	for (UEdMode* Mode : ActiveScriptableModes)
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

void FEditorModeTools::DeactivateModeAtIndex(int32 InIndex)
{
	check( InIndex >= 0 && InIndex < ActiveModes.Num() );

	TSharedPtr<FEdMode> Mode = ActiveModes[InIndex];

	Mode->Exit();

	// Remove the toolbar widget
	ActiveToolBarRows.RemoveAll(
		[&Mode](FEdModeToolbarRow& Row)
		{
			return Row.ModeID == Mode->GetID();
		}
	);

	RebuildModeToolBar();

	RecycledModes.Add(Mode->GetID(), Mode);
	ActiveModes.RemoveAt(InIndex);
}

void FEditorModeTools::DeactivateScriptableModeAtIndex(int32 InIndex)
{
	check(InIndex >= 0 && InIndex < ActiveScriptableModes.Num());

	UEdMode* Mode = ActiveScriptableModes[InIndex];

	Mode->Exit();

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
				PaletteSwitcher 
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
			ToolkitHost.Pin()->GetTabManager()->InvokeTab(EditorModeToolbarTabName);
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

			FEdMode* Mode = GetActiveMode(InModeID);
			TSharedPtr<FModeToolkit> RowToolkit = Mode->GetToolkit();

			TSharedPtr<SWidget> ActiveWidget = ModeToolbarPaletteSwitcher.Pin()->GetActiveWidget();
			if (ActiveWidget.Get() != Row.ToolbarWidget.Get())
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
	for( int32 Index = ActiveModes.Num() - 1; Index >= 0; --Index )
	{
		auto& Mode = ActiveModes[Index];
		if( Mode->GetID() == InID )
		{
			DeactivateModeAtIndex(Index);
			break;
		}
	}

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


	if( (ActiveModes.Num() == 0) && (ActiveScriptableModes.Num() == 0))
	{
		// Ensure the default mode is active if there are no active modes.
		ActivateDefaultMode();
	}
}

void FEditorModeTools::DeactivateAllModes()
{
	for( int32 Index = ActiveModes.Num() - 1; Index >= 0; --Index )
	{
		DeactivateModeAtIndex(Index);
	}

	for (int32 Index = ActiveScriptableModes.Num() - 1; Index >= 0; --Index)
	{
		DeactivateScriptableModeAtIndex(Index);
	}
}

FEdMode* FEditorModeTools::FindMode(FEditorModeID InID)
{
	return GetActiveMode(InID);
}

void FEditorModeTools::DestroyMode( FEditorModeID InID )
{
	// Find the mode from the ID and exit it.
	for( int32 Index = ActiveModes.Num() - 1; Index >= 0; --Index )
	{
		auto& Mode = ActiveModes[Index];
		if ( Mode->GetID() == InID )
		{
			// Deactivate and destroy
			DeactivateModeAtIndex(Index);
			break;
		}
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

	RecycledModes.Remove(InID);
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
	// This could ideally ask each active mode if it has any tools but when developing a new mode the toolbox not appearing by default could be hard to understand
	return !IsModeActive(FBuiltinEditorModes::EM_Default);
}

void FEditorModeTools::ActivateMode(FEditorModeID InID, bool bToggle)
{
	static bool bReentrant = false;
	if( !bReentrant )
	{
		if (InID == FBuiltinEditorModes::EM_Default)
		{
			bReentrant = true;

			for( const FEditorModeID ModeID : DefaultModeIDs )
			{
				ActivateMode( ModeID );
			}

			for( const FEditorModeID ModeID : DefaultModeIDs )
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

	const TSharedRef<IEditorModeFactory>* ModeFactory = FEditorModeRegistry::Get().GetFactoryMap().Find(InID);
	if (ModeFactory)
	{
		if (!(*ModeFactory)->ForScriptableMode())
		{
			// Recycle a mode or factory a new one
			TSharedPtr<FEdMode> Mode = RecycledModes.FindRef(InID);

			if (Mode.IsValid())
			{
				RecycledModes.Remove(InID);
			}
			else
			{
				Mode = FEditorModeRegistry::Get().CreateMode(InID, *this);
			}

			if (!Mode.IsValid())
			{
				UE_LOG(LogEditorModes, Log, TEXT("FEditorModeTools::ActivateMode : Couldn't find mode '%s'."), *InID.ToString());
				// Just return and leave the mode list unmodified
				return;
			}

			// Remove anything that isn't compatible with this mode
			for (int32 ModeIndex = ActiveModes.Num() - 1; ModeIndex >= 0; --ModeIndex)
			{
				const bool bModesAreCompatible = Mode->IsCompatibleWith(ActiveModes[ModeIndex]->GetID()) || ActiveModes[ModeIndex]->IsCompatibleWith(Mode->GetID());
				if (!bModesAreCompatible)
				{
					DeactivateModeAtIndex(ModeIndex);
				}
			}
			// Remove anything that isn't compatible with this mode
			for (int32 ModeIndex = ActiveScriptableModes.Num() - 1; ModeIndex >= 0; --ModeIndex)
			{
				const bool bModesAreCompatible = Mode->IsCompatibleWith(ActiveScriptableModes[ModeIndex]->GetID()) || ActiveScriptableModes[ModeIndex]->IsCompatibleWith(Mode->GetID());
				if (!bModesAreCompatible)
				{
					DeactivateScriptableModeAtIndex(ModeIndex);
				}
			}

			ActiveModes.Add(Mode);
			// Enter the new mode
			Mode->Enter();

			// Ask the mode to build the toolbar.
			TSharedPtr<FUICommandList> CommandList;
			const TSharedPtr<FModeToolkit> Toolkit = Mode->GetToolkit();
			if (Toolkit.IsValid())
			{
				CommandList = Toolkit->GetToolkitCommands();

				// Also build the toolkit here 
				int32 PaletteCount = 0;
				TArray<FName> PaletteNames;
				Toolkit->GetToolPaletteNames(PaletteNames);
				for(auto Palette : PaletteNames)
				{
					FToolBarBuilder ModeToolbarBuilder(CommandList, FMultiBoxCustomization(Mode->GetModeInfo().ToolbarCustomizationName), TSharedPtr<FExtender>(), Orient_Horizontal, false);
					ModeToolbarBuilder.SetStyle(&FEditorStyle::Get(), "PaletteToolBar");
					Toolkit->BuildToolPalette(Palette, ModeToolbarBuilder);

					ActiveToolBarRows.Emplace(Mode->GetID(), Palette, Toolkit->GetToolPaletteDisplayName(Palette), ModeToolbarBuilder.MakeWidget());
					PaletteCount++;
				}

				if (PaletteCount > 0) 
				{
					SpawnOrUpdateModeToolbar();
				}
			}

		}
		else
		{
			// Recycle a mode or factory a new one
			UEdMode* Mode = RecycledScriptableModes.FindRef(InID);

			if (Mode)
			{
				RecycledScriptableModes.Remove(InID);
			}
			else
			{
				Mode = FEditorModeRegistry::Get().CreateScriptableMode(InID, *this);
			}

			if (!Mode)
			{
				UE_LOG(LogEditorModes, Log, TEXT("FEditorModeTools::ActivateMode : Couldn't find mode '%s'."), *InID.ToString());
				// Just return and leave the mode list unmodified
				return;
			}

			// Remove anything that isn't compatible with this mode
			for (int32 ModeIndex = ActiveModes.Num() - 1; ModeIndex >= 0; --ModeIndex)
			{
				const bool bModesAreCompatible = Mode->IsCompatibleWith(ActiveModes[ModeIndex]->GetID()) || ActiveModes[ModeIndex]->IsCompatibleWith(Mode->GetID());
				if (!bModesAreCompatible)
				{
					DeactivateModeAtIndex(ModeIndex);
				}
			}
			// Remove anything that isn't compatible with this mode
			for (int32 ModeIndex = ActiveScriptableModes.Num() - 1; ModeIndex >= 0; --ModeIndex)
			{
				const bool bModesAreCompatible = Mode->IsCompatibleWith(ActiveScriptableModes[ModeIndex]->GetID()) || ActiveScriptableModes[ModeIndex]->IsCompatibleWith(Mode->GetID());
				if (!bModesAreCompatible)
				{
					DeactivateScriptableModeAtIndex(ModeIndex);
				}
			}

			ActiveScriptableModes.Add(Mode);
			// Enter the new mode
			Mode->Enter();

			// Ask the mode to build the toolbar.
			TSharedPtr<FUICommandList> CommandList;
			const TSharedPtr<FModeToolkit> Toolkit = Mode->GetToolkit();
			if (Toolkit.IsValid())
			{
				CommandList = Toolkit->GetToolkitCommands();

				// Also build the toolkit here 
				int32 PaletteCount = 0;
				TArray<FName> PaletteNames;
				Toolkit->GetToolPaletteNames(PaletteNames);
				for(auto Palette : PaletteNames)
				{
					FToolBarBuilder ModeToolbarBuilder(CommandList, FMultiBoxCustomization(Mode->GetModeInfo().ToolbarCustomizationName), TSharedPtr<FExtender>(), Orient_Horizontal, false);
					Toolkit->BuildToolPalette(Palette, ModeToolbarBuilder);

					ActiveToolBarRows.Emplace(Mode->GetID(), Palette, Toolkit->GetToolPaletteDisplayName(Palette), ModeToolbarBuilder.MakeWidget());
					PaletteCount++;
				}

				if (PaletteCount > 0) 
				{
					SpawnOrUpdateModeToolbar();
				}
			}
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
	for (const auto& Mode : ActiveModes)
	{
		if (Mode->GetCustomDrawingCoordinateSystem(Matrix, nullptr))
		{
			CustomCoordinateSystemProvided = true;
			break;
		}
	}

	if (!CustomCoordinateSystemProvided)
	{
		const int32 Num = GetSelectedActors()->CountSelections<AActor>();

		// Coordinate system needs to come from the last actor selected
		if (Num > 0)
		{
			Matrix = FQuatRotationMatrix(GetSelectedActors()->GetBottom<AActor>()->GetActorQuat());
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
	for( int Index = ActiveModes.Num() - 1; Index >= 0 ; Index-- )
	{
		if ( ActiveModes[Index]->ShouldDrawWidget() )
		{
			OutAxis = ActiveModes[Index]->GetWidgetAxisToDraw( InWidgetMode );
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

	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bTransactionHandled |= Mode->StartTracking(InViewportClient, InViewport);
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bTransactionHandled |= Mode->StartTracking(InViewportClient, InViewportClient->Viewport);
	}

	return bTransactionHandled;
}

/** Mouse tracking interface.  Passes tracking messages to all active modes */
bool FEditorModeTools::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bIsTracking = false;
	bool bTransactionHandled = false;

	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bTransactionHandled |= Mode->EndTracking(InViewportClient, InViewportClient->Viewport);
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bTransactionHandled |= Mode->EndTracking(InViewportClient, InViewportClient->Viewport);
	}

	CachedLocation = PivotLocation;	// Clear the pivot location
	
	return bTransactionHandled;
}

bool FEditorModeTools::AllowsViewportDragTool() const
{
	bool bCanUseDragTool = false;
	for (const TSharedPtr<FEdMode>& Mode : ActiveModes)
	{
		bCanUseDragTool |= Mode->AllowsViewportDragTool();
	}
	return bCanUseDragTool;
}

/** Notifies all active modes that a map change has occured */
void FEditorModeTools::MapChangeNotify()
{
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		Mode->MapChangeNotify();
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		Mode->MapChangeNotify();
	}
}


/** Notifies all active modes to empty their selections */
void FEditorModeTools::SelectNone()
{
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		Mode->SelectNone();
	}
}

/** Notifies all active modes of box selection attempts */
bool FEditorModeTools::BoxSelect( FBox& InBox, bool InSelect )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->BoxSelect( InBox, InSelect );
	}
	return bHandled;
}

/** Notifies all active modes of frustum selection attempts */
bool FEditorModeTools::FrustumSelect( const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->FrustumSelect( InFrustum, InViewportClient, InSelect );
	}
	return bHandled;
}


/** true if any active mode uses a transform widget */
bool FEditorModeTools::UsesTransformWidget() const
{
	bool bUsesTransformWidget = false;
	for( const auto& Mode : ActiveModes)
	{
		bUsesTransformWidget |= Mode->UsesTransformWidget();
	}

	return bUsesTransformWidget;
}

/** true if any active mode uses the passed in transform widget */
bool FEditorModeTools::UsesTransformWidget( FWidget::EWidgetMode CheckMode ) const
{
	bool bUsesTransformWidget = false;
	for( const auto& Mode : ActiveModes)
	{
		bUsesTransformWidget |= Mode->UsesTransformWidget(CheckMode);
	}

	return bUsesTransformWidget;
}

/** Sets the current widget axis */
void FEditorModeTools::SetCurrentWidgetAxis( EAxisList::Type NewAxis )
{
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		Mode->SetCurrentWidgetAxis( NewAxis );
	}
}

/** Notifies all active modes of mouse click messages. */
bool FEditorModeTools::HandleClick(FEditorViewportClient* InViewportClient,  HHitProxy *HitProxy, const FViewportClick& Click )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->HandleClick(InViewportClient, HitProxy, Click);
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->HandleClick(InViewportClient, HitProxy, Click);
	}

	return bHandled;
}

bool FEditorModeTools::ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox)
{
	bool bHandled = false;
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ModeIndex];
		bHandled |= Mode->ComputeBoundingBoxForViewportFocus(Actor, PrimitiveComponent, InOutBox);
	}

	return bHandled;
}

/** true if the passed in brush actor should be drawn in wireframe */	
bool FEditorModeTools::ShouldDrawBrushWireframe( AActor* InActor ) const
{
	bool bShouldDraw = false;
	for( const auto& Mode : ActiveModes)
	{
		bShouldDraw |= Mode->ShouldDrawBrushWireframe( InActor );
	}

	for (const auto& Mode : ActiveScriptableModes)
	{
		bShouldDraw |= Mode->ShouldDrawBrushWireframe(InActor);
	}

	if(( ActiveModes.Num() == 0 ) && (ActiveScriptableModes.Num() == 0))
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
	for( int32 Index = ActiveModes.Num() - 1; Index >= 0; --Index)
	{
		if (ActiveModes[Index]->IsPendingDeletion())
		{
			DeactivateModeAtIndex(Index);
		}
	}
	
	// Remove anything pending destruction
	for (int32 Index = ActiveScriptableModes.Num() - 1; Index >= 0; --Index)
	{
		if (ActiveScriptableModes[Index]->IsPendingDeletion())
		{
			DeactivateScriptableModeAtIndex(Index);
		}
	}


	if ((ActiveModes.Num() == 0) && ActiveScriptableModes.Num() == 0)
	{
		// Ensure the default mode is active if there are no active modes.
		ActivateDefaultMode();
	}


	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		Mode->Tick( ViewportClient, DeltaTime );
	}


	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		Mode->Tick(ViewportClient, DeltaTime);
	}
}

/** Notifies all active modes of any change in mouse movement */
bool FEditorModeTools::InputDelta( FEditorViewportClient* InViewportClient,FViewport* InViewport,FVector& InDrag,FRotator& InRot,FVector& InScale )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->InputDelta( InViewportClient, InViewport, InDrag, InRot, InScale );
	}

	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
	}
	return bHandled;
}

/** Notifies all active modes of captured mouse movement */	
bool FEditorModeTools::CapturedMouseMove( FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->CapturedMouseMove( InViewportClient, InViewport, InMouseX, InMouseY );
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->CapturedMouseMove(InViewportClient, InViewport, InMouseX, InMouseY);
	}
	return bHandled;
}

/** Notifies all active modes of all captured mouse movement */	
bool FEditorModeTools::ProcessCapturedMouseMoves( FEditorViewportClient* InViewportClient, FViewport* InViewport, const TArrayView<FIntPoint>& CapturedMouseMoves )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->ProcessCapturedMouseMoves( InViewportClient, InViewport, CapturedMouseMoves );
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->ProcessCapturedMouseMoves(InViewportClient, InViewport, CapturedMouseMoves);
	}
	return bHandled;
}

/** Notifies all active modes of keyboard input */
bool FEditorModeTools::InputKey(FEditorViewportClient* InViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->InputKey( InViewportClient, Viewport, Key, Event );
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->InputKey(InViewportClient, Viewport, Key, Event);
	}
	return bHandled;
}

/** Notifies all active modes of axis movement */
bool FEditorModeTools::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->InputAxis( InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime );
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->InputAxis(InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
	}
	return bHandled;
}

bool FEditorModeTools::GetPivotForOrbit( FVector& Pivot ) const
{
	// Just return the first pivot point specified by a mode
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		if ( Mode->GetPivotForOrbit( Pivot ) )
		{
			return true;
		}
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		if (Mode->GetPivotForOrbit(Pivot))
		{
			return true;
		}
	}
	return false;
}

bool FEditorModeTools::MouseEnter( FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 X, int32 Y )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->MouseEnter( InViewportClient, Viewport, X, Y );
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->MouseEnter(InViewportClient, Viewport, X, Y);
	}
	return bHandled;
}

bool FEditorModeTools::MouseLeave( FEditorViewportClient* InViewportClient, FViewport* Viewport )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->MouseLeave( InViewportClient, Viewport );
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->MouseLeave(InViewportClient, Viewport);
	}
	return bHandled;
}

/** Notifies all active modes that the mouse has moved */
bool FEditorModeTools::MouseMove( FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 X, int32 Y )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->MouseMove( InViewportClient, Viewport, X, Y );
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->MouseMove(InViewportClient, Viewport, X, Y);
	}
	return bHandled;
}

bool FEditorModeTools::ReceivedFocus( FEditorViewportClient* InViewportClient, FViewport* Viewport )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->ReceivedFocus( InViewportClient, Viewport );
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->ReceivedFocus(InViewportClient, Viewport);
	}
	return bHandled;
}

bool FEditorModeTools::LostFocus( FEditorViewportClient* InViewportClient, FViewport* Viewport )
{
	bool bHandled = false;
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
		bHandled |= Mode->LostFocus( InViewportClient, Viewport );
	}
	for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
	{
		UEdMode* Mode = ActiveScriptableModes[ModeIndex];
		bHandled |= Mode->LostFocus(InViewportClient, Viewport);
	}
	return bHandled;
}

/** Draws all active mode components */	
void FEditorModeTools::DrawActiveModes( const FSceneView* InView, FPrimitiveDrawInterface* PDI )
{
	for( const auto& Mode : ActiveModes)
	{
		Mode->Draw( InView, PDI );
	}
	for (const auto& Mode : ActiveScriptableModes)
	{
		Mode->Draw(InView, PDI);
	}
}

/** Renders all active modes */
void FEditorModeTools::Render( const FSceneView* InView, FViewport* Viewport, FPrimitiveDrawInterface* PDI )
{
	for( const auto& Mode : ActiveModes)
	{
		Mode->Render( InView, Viewport, PDI );
	}
	for (const auto& Mode : ActiveScriptableModes)
	{
		Mode->Render(InView, Viewport, PDI);
	}
}

/** Draws the HUD for all active modes */
void FEditorModeTools::DrawHUD( FEditorViewportClient* InViewportClient,FViewport* Viewport, const FSceneView* View, FCanvas* Canvas )
{
	for( const auto& Mode : ActiveModes)
	{
		Mode->DrawHUD( InViewportClient, Viewport, View, Canvas );
	}
	for (const auto& Mode : ActiveScriptableModes)
	{
		Mode->DrawHUD(InViewportClient, Viewport, View, Canvas);
	}
}

/** Calls PostUndo on all active modes */
void FEditorModeTools::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
		{
			const TSharedPtr<FEdMode>& Mode = ActiveModes[ ModeIndex ];
			Mode->PostUndo();
		}

		for (int32 ModeIndex = 0; ModeIndex < ActiveScriptableModes.Num(); ++ModeIndex)
		{
			UEdMode* Mode = ActiveScriptableModes[ModeIndex];
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
	for( const auto& Mode : ActiveModes)
	{
		bAllow |= Mode->AllowWidgetMove();
	}
	return bAllow;
}

bool FEditorModeTools::DisallowMouseDeltaTracking() const
{
	bool bDisallow = false;
	for( const auto& Mode : ActiveModes)
	{
		bDisallow |= Mode->DisallowMouseDeltaTracking();
	}
	for (const auto& Mode : ActiveScriptableModes)
	{
		bDisallow |= Mode->DisallowMouseDeltaTracking();
	}
	return bDisallow;
}

bool FEditorModeTools::GetCursor(EMouseCursor::Type& OutCursor) const
{
	bool bHandled = false;
	for( const auto& Mode : ActiveModes)
	{
		bHandled |= Mode->GetCursor(OutCursor);
	}
	for (const auto& Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->GetCursor(OutCursor);
	}
	return bHandled;
}

bool FEditorModeTools::GetOverrideCursorVisibility(bool& bWantsOverride, bool& bHardwareCursorVisible, bool bSoftwareCursorVisible) const
{
	bool bHandled = false;
	for (const auto& Mode : ActiveModes)
	{
		bHandled |= Mode->GetOverrideCursorVisibility(bWantsOverride, bHardwareCursorVisible, bSoftwareCursorVisible);
	}
	for (const auto& Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->GetOverrideCursorVisibility(bWantsOverride, bHardwareCursorVisible, bSoftwareCursorVisible);
	}
	return bHandled;
}

bool FEditorModeTools::PreConvertMouseMovement(FEditorViewportClient* InViewportClient)
{
	bool bHandled = false;
	for (const auto& Mode : ActiveModes)
	{
		bHandled |= Mode->PreConvertMouseMovement(InViewportClient);
	}
	for (const auto& Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->PreConvertMouseMovement(InViewportClient);
	}
	return bHandled;
}

bool FEditorModeTools::PostConvertMouseMovement(FEditorViewportClient* InViewportClient)
{
	bool bHandled = false;
	for (const auto& Mode : ActiveModes)
	{
		bHandled |= Mode->PostConvertMouseMovement(InViewportClient);
	}
	for (const auto& Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->PostConvertMouseMovement(InViewportClient);
	}
	return bHandled;
}

bool FEditorModeTools::GetShowWidget() const
{
	bool bDrawModeSupportsWidgetDrawing = false;
	// Check to see of any active modes support widget drawing
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		bDrawModeSupportsWidgetDrawing |= ActiveModes[ModeIndex]->ShouldDrawWidget();
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
	for (int Index = ActiveModes.Num() - 1; Index >= 0 ; Index--)
	{
		if ( ActiveModes[Index]->UsesTransformWidget() )
		{
			 return ActiveModes[Index]->GetWidgetLocation();
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

bool FEditorModeTools::GetShowFriendlyVariableNames() const
{
	return GetDefault<UEditorStyleSettings>()->bShowFriendlyNames;
}

const uint32 FEditorModeTools::GetMaxNumberOfBookmarks(FEditorViewportClient* InViewportClient) const
{
	return IBookmarkTypeTools::Get().GetMaxNumberOfBookmarks(InViewportClient);
}

void FEditorModeTools::CompactBookmarks(FEditorViewportClient* InViewportClient) const
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

void FEditorModeTools::JumpToBookmark( uint32 InIndex, bool bShouldRestoreLevelVisibility, FEditorViewportClient* InViewportClient )
{
	const IBookmarkTypeTools& BookmarkTools = IBookmarkTypeTools::Get();
	TSharedPtr<FBookmarkBaseJumpToSettings> JumpToSettings;

	if (BookmarkTools.GetBookmarkClass(InViewportClient) == UBookMark::StaticClass())
	{
		TSharedPtr<FBookmarkJumpToSettings> Settings = MakeShareable<FBookmarkJumpToSettings>(new FBookmarkJumpToSettings);
		Settings->bShouldRestorLevelVisibility = bShouldRestoreLevelVisibility;
	}

	IBookmarkTypeTools::Get().JumpToBookmark(InIndex, JumpToSettings, InViewportClient);
}

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
	for( int32 x = 0 ; x < ActiveModes.Num() ; ++x )
	{
		ActiveModes[x]->AddReferencedObjects( Collector );
	}

	Collector.AddReferencedObjects(ActiveScriptableModes);
}

FEdMode* FEditorModeTools::GetActiveMode( FEditorModeID InID )
{
	for( auto& Mode : ActiveModes )
	{
		if( Mode->GetID() == InID )
		{
			return Mode.Get();
		}
	}
	return nullptr;
}

const FEdMode* FEditorModeTools::GetActiveMode( FEditorModeID InID ) const
{
	for (const auto& Mode : ActiveModes)
	{
		if (Mode->GetID() == InID)
		{
			return Mode.Get();
		}
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
	for( const FEditorModeID ModeID : DefaultModeIDs )
	{
		if( !IsModeActive( ModeID ) )
		{
			bAllDefaultModesActive = false;
			break;
		}
	}
	return bAllDefaultModesActive;
}

void FEditorModeTools::GetActiveModes( TArray<FEdMode*>& OutActiveModes )
{
	OutActiveModes.Empty();
	// Copy into an array.  Do not let users modify the active list directly.
	for( auto& Mode : ActiveModes)
	{
		OutActiveModes.Add(Mode.Get());
	}
}
bool FEditorModeTools::CanCycleWidgetMode() const
{
	for (auto& Mode : ActiveModes)
	{
		if (Mode->CanCycleWidgetMode())
		{
			return true;
		}
	}
	return false;
}


bool FEditorModeTools::CanAutoSave() const
{
	for (auto& Mode : ActiveModes)
	{
		if (Mode->CanAutoSave() == false)
		{
			return false;
		}
	}

	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->CanAutoSave() == false)
		{
			return false;
		}
	}

	return true;
}
