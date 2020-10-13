// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "Editor/GroupActor.h"
#include "Components/ChildActorComponent.h"
#include "Components/DecalComponent.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Engine/Selection.h"
#include "EdMode.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "Dialogs/Dialogs.h"
#include "UnrealEdGlobals.h"
#include "ScopedTransaction.h"
#include "Engine/LevelStreaming.h"
#include "LevelUtils.h"
#include "StatsViewerModule.h"
#include "SnappingUtils.h"
#include "Logging/MessageLog.h"
#include "ActorGroupingUtils.h"
#include "Subsystems/BrushEditingSubsystem.h"
#include "TypedElementList.h"
#include "Elements/TypedElementSelectionSet.h"

#define LOCTEXT_NAMESPACE "EditorSelectUtils"

DEFINE_LOG_CATEGORY_STATIC(LogEditorSelectUtils, Log, All);

/*-----------------------------------------------------------------------------
	Globals.
-----------------------------------------------------------------------------*/


// Click flags.
enum EViewportClick
{
	CF_MOVE_ACTOR	= 1,	// Set if the actors have been moved since first click
	CF_MOVE_TEXTURE = 2,	// Set if textures have been adjusted since first click
	CF_MOVE_ALL     = (CF_MOVE_ACTOR | CF_MOVE_TEXTURE),
};

/*-----------------------------------------------------------------------------
   Change transacting.
-----------------------------------------------------------------------------*/


void UUnrealEdEngine::NoteActorMovement()
{
	if( !GUndo && !(GEditor->ClickFlags & CF_MOVE_ACTOR) )
	{
		GEditor->ClickFlags |= CF_MOVE_ACTOR;

		const FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "ActorMovement", "Actor Movement") );
		GLevelEditorModeTools().Snapping=0;
		
		AActor* SelectedActor = NULL;
		for ( FSelectionIterator It( GetSelectedActorIterator() ) ; It ; ++It )
		{
			AActor* Actor = static_cast<AActor*>( *It );
			checkSlow( Actor->IsA(AActor::StaticClass()) );

			SelectedActor = Actor;
			break;
		}

		if( SelectedActor == NULL )
		{
			USelection* SelectedActors = GetSelectedActors();
			SelectedActors->Modify();
			SelectActor( GWorld->GetDefaultBrush(), true, true );
		}

		// Look for an actor that requires snapping.
		for ( FSelectionIterator It( GetSelectedActorIterator() ) ; It ; ++It )
		{
			AActor* Actor = static_cast<AActor*>( *It );
			checkSlow( Actor->IsA(AActor::StaticClass()) );

			GLevelEditorModeTools().Snapping = 1;
			break;
		}

		TSet<AGroupActor*> GroupActors;

		// Modify selected actors.
		for ( FSelectionIterator It( GetSelectedActorIterator() ) ; It ; ++It )
		{
			AActor* Actor = static_cast<AActor*>( *It );
			checkSlow( Actor->IsA(AActor::StaticClass()) );

			Actor->Modify();

			if (UActorGroupingUtils::IsGroupingActive())
			{
				// if this actor is in a group, add the GroupActor into a list to be modified shortly
				AGroupActor* ActorLockedRootGroup = AGroupActor::GetRootForActor(Actor, true);
				if (ActorLockedRootGroup != nullptr)
				{
					GroupActors.Add(ActorLockedRootGroup);
				}
			}
		}

		// Modify unique group actors
		for (auto* GroupActor : GroupActors)
		{
			GroupActor->Modify();
		}
	}
}

void UUnrealEdEngine::FinishAllSnaps()
{
	if(!IsRunningCommandlet())
	{
		if( ClickFlags & CF_MOVE_ACTOR )
		{
			ClickFlags &= ~CF_MOVE_ACTOR;

			for ( FSelectionIterator It( GetSelectedActorIterator() ) ; It ; ++It )
			{
				AActor* Actor = static_cast<AActor*>( *It );
				checkSlow( Actor->IsA(AActor::StaticClass()) );

				Actor->Modify();
				Actor->InvalidateLightingCache();
				Actor->PostEditMove( true );
			}
		}
	}
}


void UUnrealEdEngine::Cleanse( bool ClearSelection, bool Redraw, const FText& Reason, bool bResetTrans )
{
	if (GIsRunning)
	{
		FMessageLog("MapCheck").NewPage(LOCTEXT("MapCheck", "Map Check"));

		FMessageLog("LightingResults").NewPage(LOCTEXT("LightingBuildNewLogPage", "Lighting Build"));

		FStatsViewerModule& StatsViewerModule = FModuleManager::Get().LoadModuleChecked<FStatsViewerModule>(TEXT("StatsViewer"));
		StatsViewerModule.Clear();
	}

	Super::Cleanse( ClearSelection, Redraw, Reason, bResetTrans );
}


FVector UUnrealEdEngine::GetPivotLocation()
{
	return GLevelEditorModeTools().PivotLocation;
}


void UUnrealEdEngine::SetPivot( FVector NewPivot, bool bSnapPivotToGrid, bool bIgnoreAxis, bool bAssignPivot/*=false*/ )
{
	FEditorModeTools& EditorModeTools = GLevelEditorModeTools();

	if( !bIgnoreAxis )
	{
		// Don't stomp on orthonormal axis.
		// TODO: this breaks if there is genuinely a need to set the pivot to a coordinate containing a zero component
 		if( NewPivot.X==0 ) NewPivot.X=EditorModeTools.PivotLocation.X;
 		if( NewPivot.Y==0 ) NewPivot.Y=EditorModeTools.PivotLocation.Y;
 		if( NewPivot.Z==0 ) NewPivot.Z=EditorModeTools.PivotLocation.Z;
	}

	// Set the pivot.
	EditorModeTools.SetPivotLocation(NewPivot, false);

	if( bSnapPivotToGrid )
	{
		FRotator DummyRotator(0,0,0);
		FSnappingUtils::SnapToBSPVertex( EditorModeTools.SnappedLocation, EditorModeTools.GridBase, DummyRotator );
		EditorModeTools.PivotLocation = EditorModeTools.SnappedLocation;
	}

	// Check all actors.
	int32 Count=0, SnapCount=0;

	//default to using the x axis for the translate rotate widget
	EditorModeTools.TranslateRotateXAxisAngle = 0.0f;
	EditorModeTools.TranslateRotate2DAngle = 0.0f;
	FVector TranslateRotateWidgetWorldXAxis;

	FVector Widget2DWorldXAxis;

	AActor* LastSelectedActor = NULL;
	for ( FSelectionIterator It( GetSelectedActorIterator() ) ; It ; ++It )
	{
		AActor* Actor = static_cast<AActor*>( *It );
		checkSlow( Actor->IsA(AActor::StaticClass()) );

		if (Count==0)
		{
			TranslateRotateWidgetWorldXAxis = Actor->ActorToWorld().TransformVector(FVector(1.0f, 0.0f, 0.0f));
			//get the xy plane project of this vector
			TranslateRotateWidgetWorldXAxis.Z = 0.0f;
			if (!TranslateRotateWidgetWorldXAxis.Normalize())
			{
				TranslateRotateWidgetWorldXAxis = FVector(1.0f, 0.0f, 0.0f);
			}

			Widget2DWorldXAxis = Actor->ActorToWorld().TransformVector(FVector(1, 0, 0));
			Widget2DWorldXAxis.Y = 0;
			if (!Widget2DWorldXAxis.Normalize())
			{
				Widget2DWorldXAxis = FVector(1, 0, 0);
			}
		}

		LastSelectedActor = Actor;
		++Count;
		++SnapCount;
	}
	
	if( bAssignPivot && LastSelectedActor && UActorGroupingUtils::IsGroupingActive())
	{
		// set group pivot for the root-most group
		AGroupActor* ActorGroupRoot = AGroupActor::GetRootForActor(LastSelectedActor, true, true);
		if(ActorGroupRoot)
		{
			ActorGroupRoot->SetActorLocation( EditorModeTools.PivotLocation, false );
		}
	}

	//if there are multiple actors selected, just use the x-axis for the "translate/rotate" or 2D widgets
	if (Count == 1)
	{
		EditorModeTools.TranslateRotateXAxisAngle = TranslateRotateWidgetWorldXAxis.Rotation().Yaw;
		EditorModeTools.TranslateRotate2DAngle = FMath::RadiansToDegrees(FMath::Atan2(Widget2DWorldXAxis.Z, Widget2DWorldXAxis.X));
	}

	// Update showing.
	EditorModeTools.PivotShown = SnapCount>0 || Count>1;
}


void UUnrealEdEngine::ResetPivot()
{
	GLevelEditorModeTools().PivotShown	= 0;
	GLevelEditorModeTools().Snapping		= 0;
	GLevelEditorModeTools().SnappedActor	= 0;
}

/*-----------------------------------------------------------------------------
	Selection.
-----------------------------------------------------------------------------*/

void UUnrealEdEngine::OnEditorSelectionElementListPtrChanged(USelection* Selection, UTypedElementList* OldElementList, UTypedElementList* NewElementList)
{
	if (Selection == GetSelectedActors())
	{
		if (OldElementList)
		{
			OldElementList->OnChanged().RemoveAll(this);
		}

		if (NewElementList)
		{
			NewElementList->OnChanged().AddUObject(this, &UUnrealEdEngine::OnEditorSelectionElementListChanged);
		}
	}
}


void UUnrealEdEngine::OnEditorSelectionElementListChanged(const UTypedElementList* ElementList)
{
	NoteSelectionChange();
}


void UUnrealEdEngine::PostActorSelectionChanged()
{
	// Whenever selection changes, recompute whether the selection contains a locked actor
	bCheckForLockActors = true;

	// Whenever selection changes, recompute whether the selection contains a world info actor
	bCheckForWorldSettingsActors = true;
}


void UUnrealEdEngine::SetPivotMovedIndependently(bool bMovedIndependently)
{
	bPivotMovedIndependently = bMovedIndependently;
}


bool UUnrealEdEngine::IsPivotMovedIndependently() const
{
	return bPivotMovedIndependently;
}


void UUnrealEdEngine::UpdatePivotLocationForSelection( bool bOnChange )
{
	// Pick a new common pivot, or not.
	AActor* SingleActor = nullptr;
	USceneComponent* SingleComponent = nullptr;

	if (GetSelectedComponentCount() > 0)
	{
		for (FSelectedEditableComponentIterator It(*GetSelectedComponents()); It; ++It)
		{
			UActorComponent* Component = CastChecked<UActorComponent>(*It);
			AActor* ComponentOwner = Component->GetOwner();

			if (ComponentOwner != nullptr)
			{
				USelection* SelectedActors = GetSelectedActors();
				const bool bIsOwnerSelected = SelectedActors->IsSelected(ComponentOwner);
				ensureMsgf(bIsOwnerSelected, TEXT("Owner(%s) of %s is not selected"), *ComponentOwner->GetFullName(), *Component->GetFullName());

				if (ComponentOwner->GetWorld() == GWorld)
				{
					SingleActor = ComponentOwner;
					if (Component->IsA<USceneComponent>())
					{
						SingleComponent = CastChecked<USceneComponent>(Component);
					}

					const bool IsTemplate = ComponentOwner->IsTemplate();
					const bool LevelLocked = !FLevelUtils::IsLevelLocked(ComponentOwner->GetLevel());
					check(IsTemplate || LevelLocked);
				}
			}
		}
	}
	else
	{
		for (FSelectionIterator It(GetSelectedActorIterator()); It; ++It)
		{
			AActor* Actor = static_cast<AActor*>(*It);
			checkSlow(Actor->IsA(AActor::StaticClass()));

			const bool IsTemplate = Actor->IsTemplate();
			const bool LevelLocked = !FLevelUtils::IsLevelLocked(Actor->GetLevel());
			check(IsTemplate || LevelLocked);

			SingleActor = Actor;
		}
	}
	
	if (SingleComponent != NULL)
	{
		SetPivot(SingleComponent->GetComponentLocation(), false, true);
	}
	else if( SingleActor != NULL ) 
	{
		UBrushEditingSubsystem* BrushSubsystem = GEditor->GetEditorSubsystem<UBrushEditingSubsystem>();
		const bool bGeometryMode = BrushSubsystem ? BrushSubsystem->IsGeometryEditorModeActive() : false;

		// For geometry mode use current pivot location as it's set to selected face, not actor
		if (!bGeometryMode || bOnChange == true)
		{
			// Set pivot point to the actor's location, accounting for any set pivot offset
			FVector PivotPoint = SingleActor->GetTransform().TransformPosition(SingleActor->GetPivotOffset());

			// If grouping is active, see if this actor is part of a locked group and use that pivot instead
			if(UActorGroupingUtils::IsGroupingActive())
			{
				AGroupActor* ActorGroupRoot = AGroupActor::GetRootForActor(SingleActor, true, true);
				if(ActorGroupRoot)
				{
					PivotPoint = ActorGroupRoot->GetActorLocation();
				}
			}
			SetPivot( PivotPoint, false, true );
		}
	}
	else
	{
		ResetPivot();
	}

	SetPivotMovedIndependently(false);
}



void UUnrealEdEngine::NoteSelectionChange(bool bNotify)
{
	// The selection changed, so make sure the pivot (widget) is located in the right place
	UpdatePivotLocationForSelection( true );

	// Clear active editing visualizer on selection change
	ComponentVisManager.ClearActiveComponentVis();

	GLevelEditorModeTools().ActorSelectionChangeNotify();

	const bool bComponentSelectionChanged = GetSelectedComponentCount() > 0;
	if (bNotify)
	{
		USelection* Selection = bComponentSelectionChanged ? GetSelectedComponents() : GetSelectedActors();
		Selection->NoteSelectionChanged();
	}
	
	if (!bComponentSelectionChanged)
	{
		PostActorSelectionChanged();
		UpdateFloatingPropertyWindows();
	}

	RedrawLevelEditingViewports();
}

void UUnrealEdEngine::SelectGroup(AGroupActor* InGroupActor, bool bForceSelection/*=false*/, bool bInSelected/*=true*/, bool bNotify/*=true*/)
{
	USelection* ActorSelection = GetSelectedActors();

	if (UTypedElementSelectionSet* SelectionSet = ActorSelection->GetElementSelectionSet())
	{
		const FTypedElementSelectionOptions SelectionOptions = FTypedElementSelectionOptions()
			.SetWarnIfLocked(true)
			.SetAllowGroups(false)
			.SetAllowLegacyNotifications(false);

		bool bSelectionChanged = false;

		// Select/deselect all actors within the group (if locked or forced)
		if (bForceSelection || InGroupActor->IsLocked())
		{
			FTypedElementListLegacySyncScopedBatch LegacySyncBatch(SelectionSet->GetMutableElementList(), SelectionOptions.AllowLegacyNotifications());

			TArray<AActor*> GroupActors;
			InGroupActor->GetGroupActors(GroupActors);
			for (AActor* Actor : GroupActors)
			{
				bSelectionChanged |= bInSelected
					? SelectionSet->SelectElement(Actor->AcquireEditorElementHandle(), SelectionOptions)
					: SelectionSet->DeselectElement(Actor->AcquireEditorElementHandle(), SelectionOptions);
			}
		}

		if (bSelectionChanged)
		{
			if (bNotify)
			{
				SelectionSet->GetMutableElementList()->NotifyPendingChanges();
			}
			else
			{
				SelectionSet->GetMutableElementList()->ClearPendingChanges();
			}
		}
	}
}

bool UUnrealEdEngine::CanSelectActor(AActor* Actor, bool bInSelected, bool bSelectEvenIfHidden, bool bWarnIfLevelLocked ) const
{
	if (!Actor)
	{
		return false;
	}

	USelection* ActorSelection = GetSelectedActors();

	if (UTypedElementSelectionSet* SelectionSet = ActorSelection->GetElementSelectionSet())
	{
		const FTypedElementSelectionOptions SelectionOptions = FTypedElementSelectionOptions()
			.SetAllowHidden(bSelectEvenIfHidden)
			.SetWarnIfLocked(bWarnIfLevelLocked);

		return bInSelected
			? SelectionSet->CanSelectElement(Actor->AcquireEditorElementHandle(), SelectionOptions)
			: SelectionSet->CanDeselectElement(Actor->AcquireEditorElementHandle(), SelectionOptions);
	}

	return false;
}

void UUnrealEdEngine::SelectActor(AActor* Actor, bool bInSelected, bool bNotify, bool bSelectEvenIfHidden, bool bForceRefresh)
{
	if (!Actor)
	{
		return;
	}

	USelection* ActorSelection = GetSelectedActors();

	if (UTypedElementSelectionSet* SelectionSet = ActorSelection->GetElementSelectionSet())
	{
		const FTypedElementSelectionOptions SelectionOptions = FTypedElementSelectionOptions()
			.SetAllowHidden(bSelectEvenIfHidden)
			.SetWarnIfLocked(true)
			.SetAllowLegacyNotifications(false);

		const bool bSelectionChanged = bInSelected
			? SelectionSet->SelectElement(Actor->AcquireEditorElementHandle(), SelectionOptions)
			: SelectionSet->DeselectElement(Actor->AcquireEditorElementHandle(), SelectionOptions);

		if (bSelectionChanged)
		{
			if (bNotify)
			{
				SelectionSet->GetMutableElementList()->NotifyPendingChanges();
			}
			else
			{
				SelectionSet->GetMutableElementList()->ClearPendingChanges();
			}
		}
		else if (bNotify || bForceRefresh)
		{
			// Reset the property windows, in case something has changed since previous selection
			UpdateFloatingPropertyWindows(bForceRefresh);
		}
	}
}

void UUnrealEdEngine::SelectComponent(UActorComponent* Component, bool bInSelected, bool bNotify, bool bSelectEvenIfHidden)
{
	if (!Component)
	{
		return;
	}

	USelection* ComponentSelection = GetSelectedComponents();

	if (UTypedElementSelectionSet* SelectionSet = ComponentSelection->GetElementSelectionSet())
	{
		const FTypedElementSelectionOptions SelectionOptions = FTypedElementSelectionOptions()
			.SetAllowHidden(bSelectEvenIfHidden)
			.SetWarnIfLocked(true)
			.SetAllowLegacyNotifications(false);

		const bool bSelectionChanged = bInSelected
			? SelectionSet->SelectElement(Component->AcquireEditorElementHandle(), SelectionOptions)
			: SelectionSet->DeselectElement(Component->AcquireEditorElementHandle(), SelectionOptions);

		if (bSelectionChanged)
		{
			if (bNotify)
			{
				SelectionSet->GetMutableElementList()->NotifyPendingChanges();
			}
			else
			{
				SelectionSet->GetMutableElementList()->ClearPendingChanges();
			}
		}
	}
}

bool UUnrealEdEngine::IsComponentSelected(const UPrimitiveComponent* PrimComponent)
{
	USelection* ComponentSelection = GetSelectedComponents();

	if (UTypedElementSelectionSet* SelectionSet = ComponentSelection->GetElementSelectionSet())
	{
		return SelectionSet->IsElementSelected(PrimComponent->AcquireEditorElementHandle(), FTypedElementIsSelectedOptions().SetAllowIndirect(true));
	}

	return false;
}

void UUnrealEdEngine::SelectBSPSurf(UModel* InModel, int32 iSurf, bool bSelected, bool bNoteSelectionChange)
{
	if( GEdSelectionLock )
	{
		return;
	}

	FBspSurf& Surf = InModel->Surfs[ iSurf ];
	InModel->ModifySurf( iSurf, false );

	if( bSelected )
	{
		Surf.PolyFlags |= PF_Selected;
	}
	else
	{
		Surf.PolyFlags &= ~PF_Selected;
	}

	if( bNoteSelectionChange )
	{
		NoteSelectionChange();
	}

	PostActorSelectionChanged();
}

/**
 * Deselects all BSP surfaces in the specified level.
 *
 * @param	Level		The level for which to deselect all surfaces.
 * @return				The number of surfaces that were deselected
 */
static uint32 DeselectAllSurfacesForLevel(ULevel* Level)
{
	uint32 NumSurfacesDeselected = 0;
	if ( Level )
	{
		UModel* Model = Level->Model;
		for( int32 SurfaceIndex = 0 ; SurfaceIndex < Model->Surfs.Num() ; ++SurfaceIndex )
		{
			FBspSurf& Surf = Model->Surfs[SurfaceIndex];
			if( Surf.PolyFlags & PF_Selected )
			{
				Model->ModifySurf( SurfaceIndex, false );
				Surf.PolyFlags &= ~PF_Selected;
				++NumSurfacesDeselected;
			}
		}
	}
	return NumSurfacesDeselected;
}

/**
 * Deselects all BSP surfaces in the specified world.
 *
 * @param	World		The world for which to deselect all surfaces.
 * @return				The number of surfaces that were deselected
 */
static uint32 DeselectAllSurfacesForWorld(UWorld* World)
{
	uint32 NumSurfacesDeselected = 0;
	if (World)
	{
		NumSurfacesDeselected += DeselectAllSurfacesForLevel(World->PersistentLevel);
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (StreamingLevel)
			{
				if (ULevel* Level = StreamingLevel->GetLoadedLevel())
				{
					NumSurfacesDeselected += DeselectAllSurfacesForLevel(Level);
				}
			}
		}
	}
	return NumSurfacesDeselected;
}

void UUnrealEdEngine::DeselectAllSurfaces()
{
	DeselectAllSurfacesForWorld(GWorld);
}

void UUnrealEdEngine::SelectNone(bool bNoteSelectionChange, bool bDeselectBSPSurfs, bool WarnAboutManyActors)
{
	if (GEdSelectionLock)
	{
		return;
	}

	bool bSelectionChanged = false;

	USelection* ActorSelection = GetSelectedActors();
	UTypedElementSelectionSet* SelectionSet = ActorSelection->GetElementSelectionSet();

	if (SelectionSet)
	{
		bSelectionChanged |= SelectionSet->ClearSelection(FTypedElementSelectionOptions().SetAllowLegacyNotifications(false));
	}

	if (bDeselectBSPSurfs)
	{
		bSelectionChanged |= DeselectAllSurfacesForWorld(GWorld) > 0;
	}

	if (bSelectionChanged)
	{
		PostActorSelectionChanged();

		if (SelectionSet)
		{
			if (bNoteSelectionChange)
			{
				SelectionSet->GetMutableElementList()->NotifyPendingChanges();
			}
			else
			{
				SelectionSet->GetMutableElementList()->ClearPendingChanges();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
