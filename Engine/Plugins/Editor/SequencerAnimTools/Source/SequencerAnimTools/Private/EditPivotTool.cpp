// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditPivotTool.h"
#include "LevelEditorSequencerIntegration.h"
#include "ISequencer.h"
#include "Sequencer/SequencerTrailHierarchy.h"
#include "Sequencer/MovieSceneTransformTrail.h"
#include "BaseGizmos/TransformGizmo.h"
#include "InteractiveGizmoManager.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/SingleKeyCaptureBehavior.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "Framework/Commands/GenericCommands.h"
#include "LevelEditor.h"

#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
// for raycast into World
#include "CollisionQueryParams.h"
#include "Engine/World.h"

#include "SceneManagement.h"
#include "ScopedTransaction.h"
#include "Tools/MotionTrailOptions.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"
#include "ControlRig.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "ILevelSequenceEditorToolkit.h"
#include "EditorModeManager.h"
#include "InteractiveToolManager.h"
#include "EdModeInteractiveToolsContext.h"

#define LOCTEXT_NAMESPACE "SequencerAnimTools"



void FEditPivotCommands::RegisterCommands()
{
	UI_COMMAND(ResetPivot, "Reset Pivot To Original", "Reset pivot back to original location", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control|EModifierKey::Shift|EModifierKey::Alt, EKeys::G));
}


TMap<TWeakObjectPtr<UControlRig>, FControlRigMappings> USequencerPivotTool::SavedPivotLocations;
TArray<FControlRigMappings> USequencerPivotTool::LastSelectedObjects;

static void GetControlRigsAndSequencer(TArray<UControlRig*>& ControlRigs, TWeakPtr<ISequencer>& SequencerPtr, ULevelSequence** LevelSequence)
{
	*LevelSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (*LevelSequence)
	{
		IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(*LevelSequence, false);
		ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
		SequencerPtr = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
		if (SequencerPtr.IsValid())
		{
			ControlRigs = UControlRigSequencerEditorLibrary::GetVisibleControlRigs();
		}
	}
}
bool USequencerPivotToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	//only start if we have anything selected  or we selected somethign last time
	//which will then get selected later once the tool starts.
	if (USequencerPivotTool::LastSelectedObjects.Num() > 0)
	{
		return true;
	}
	ULevelSequence* LevelSequence;
	TArray<UControlRig*> ControlRigs;
	TWeakPtr<ISequencer> SequencerPtr;
	GetControlRigsAndSequencer(ControlRigs, SequencerPtr, &LevelSequence);

	for (UControlRig* ControlRig : ControlRigs)
	{
		if (ControlRig && ControlRig->CurrentControlSelection().Num() > 0)
		{
			return true;
		}
	}
	
	
	return false;
}

UInteractiveTool* USequencerPivotToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	USequencerPivotTool* NewTool = NewObject<USequencerPivotTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World, SceneState.GizmoManager);
	return NewTool;
}
/*
* Proxy
*/

struct HSequencerPivotProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	HSequencerPivotProxy(EHitProxyPriority InPriority = HPP_Foreground) :
		HHitProxy(InPriority)
	{}
};

IMPLEMENT_HIT_PROXY(HSequencerPivotProxy, HHitProxy);

/*
* Tool
*/

void USequencerPivotTool::SetWorld(UWorld* World, UInteractiveGizmoManager* InGizmoManager)
{
	TargetWorld = World;
	GizmoManager = InGizmoManager;
}

void USequencerPivotTool::Setup()
{
	//when entered we check to see if shift is pressed this can change where we set the pivot on start or reset
	FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();
	bShiftPressedWhenStarted = KeyState.IsShiftDown();

	UInteractiveTool::Setup();

	ClickBehavior = NewObject<USingleClickInputBehavior>(this);
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);

	TransformProxy = NewObject<UTransformProxy>(this);

	FString GizmoIdentifier = TEXT("PivotToolGizmoIdentifier");
	TransformGizmo = GizmoManager->Create3AxisTransformGizmo(this, GizmoIdentifier);

	TransformProxy->OnTransformChanged.AddUObject(this, &USequencerPivotTool::GizmoTransformChanged);
	TransformProxy->OnBeginTransformEdit.AddUObject(this, &USequencerPivotTool::GizmoTransformStarted);
	TransformProxy->OnEndTransformEdit.AddUObject(this, &USequencerPivotTool::GizmoTransformEnded);
	TransformGizmo->SetActiveTarget(TransformProxy, GetToolManager());

	GetControlRigsAndSequencer(ControlRigs, SequencerPtr, &LevelSequence);
	UpdateTransformAndSelectionOnEntering();
	UpdateGizmoTransform();
	UpdateGizmoVisibility();
	//we get delegates last since we may select something above
	for (UControlRig* ControlRig : ControlRigs)
	{
		ControlRig->ControlSelected().AddUObject(this,&USequencerPivotTool::HandleControlSelected);
	}
	SaveLastSelected();

	CommandBindings = MakeShareable(new FUICommandList);

	FEditPivotCommands::Register();

	const FEditPivotCommands& Commands = FEditPivotCommands::Get();

	CommandBindings->MapAction(
		Commands.ResetPivot,
		FExecuteAction::CreateUObject(this, &USequencerPivotTool::ResetPivot)
		);

}

void USequencerPivotTool::ResetPivot()
{
	SetGizmoBasedOnSelection(false);
	UpdateGizmoTransform();
}

//Last selected is really the ones that were selected when you entered the tool
//we use this in case nothing was selected when the tool is active so that we instead select it.
void USequencerPivotTool::SaveLastSelected()
{
	LastSelectedObjects.SetNum(0);
	for (UControlRig* ControlRig : ControlRigs)
	{
		TArray<FName> SelectedControls = ControlRig->CurrentControlSelection();
		for (const FName& Name : SelectedControls)
		{
			FControlRigMappings Mapping;
			Mapping.ControlRig = ControlRig;
			Mapping.PivotTransforms.Add(Name, GizmoTransform);
			LastSelectedObjects.Add(Mapping);
		}
	}
}

//when we enter the tool if we have things selected we get it's last transform position
//if not we should have the last thing selected, and if so we select that.
void USequencerPivotTool::UpdateTransformAndSelectionOnEntering()
{
	//if shift was pressed when we started we don't use saved, this will move pivot to last object
	bool bhaveSomethingSelected = SetGizmoBasedOnSelection(!bShiftPressedWhenStarted);
	//okay nothing selected we select the last thing selec
	if (bhaveSomethingSelected == false)
	{
		for (FControlRigMappings& Mappings : LastSelectedObjects)
		{
			if (Mappings.ControlRig.IsValid())
			{
				for (TPair<FName, FTransform>& Pair: Mappings.PivotTransforms)
				{
					Mappings.ControlRig->SelectControl(Pair.Key, true);
				}
			}
		}
		SetGizmoBasedOnSelection(!bShiftPressedWhenStarted);
	}
}

bool USequencerPivotTool::ProcessCommandBindings(const FKey Key, const bool bRepeat) const
{
	if (CommandBindings.IsValid())
	{
		FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();
		return CommandBindings->ProcessCommandBindings(Key, KeyState, bRepeat);
	}
	return false;
}

bool USequencerPivotTool::SetGizmoBasedOnSelection(bool bUseSaved)
{

	GizmoTransform = FTransform::Identity;
	FVector Location(0.0f, 0.0f, 0.0f);
	ISequencer* Sequencer = SequencerPtr.Pin().Get();
	if (Sequencer == nullptr)
	{
		return false;
	}
	const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);

	bool bHaveSomethingSelected = false;
	for (UControlRig* ControlRig : ControlRigs)
	{
		FControlRigMappings* Mappings = SavedPivotLocations.Find(ControlRig);
		TArray<FName> SelectedControls = ControlRig->CurrentControlSelection();

		for (const FName& Name : SelectedControls)
		{
			bHaveSomethingSelected = true;
			bool bHasSavedPivot = false;
			if (bUseSaved && Mappings)
			{
				if (FTransform* Transform = Mappings->PivotTransforms.Find(Name))
				{
					GizmoTransform = *Transform;
					bHasSavedPivot = true;
				}
			}
			if (bHasSavedPivot == false)
			{
				GizmoTransform = UControlRigSequencerEditorLibrary::GetControlRigWorldTransform(LevelSequence, ControlRig, Name, FrameTime.RoundToFrame(),
					ESequenceTimeUnit::TickResolution);
			}
		}
	}
	GizmoTransform.SetScale3D(FVector(1.0f, 1.0f, 1.0f));
	GizmoTransform.SetRotation(FQuat::Identity);
	return bHaveSomethingSelected;
}
void USequencerPivotTool::Shutdown(EToolShutdownType ShutdownType)
{
	GizmoManager->DestroyAllGizmosByOwner(this);
	RemoveControlRigDelegates();
}

void USequencerPivotTool::HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected)
{

	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	{
		TSharedPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance().Pin();

		if (LevelEditorPtr.IsValid())
		{
			FString ActiveToolName = LevelEditorPtr->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->GetActiveToolName(EToolSide::Left);
			if (ActiveToolName == TEXT("SequencerPivotTool"))
			{
				LevelEditorPtr->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->DeactivateTool(EToolSide::Left, EToolShutdownType::Completed);
			}
		}
	}
	
}

void USequencerPivotTool::RemoveControlRigDelegates()
{
	for (UControlRig* ControlRig : ControlRigs)
	{
		ControlRig->ControlSelected().RemoveAll(this);
	}
}


void USequencerPivotTool::GizmoTransformStarted(UTransformProxy* Proxy)
{
	ControlRigDrags.SetNum(0);
	TransactionIndex = INDEX_NONE;
	ISequencer* Sequencer = SequencerPtr.Pin().Get();
	if (Sequencer && LevelSequence)
	{
		//TransactionIndex = GEditor->BeginTransaction(nullptr, LOCTEXT("TransformPivot", "Transform Pivot"), nullptr);
		const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
		const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
		const FFrameNumber FrameNumber = FrameTime.RoundToFrame();

		for (UControlRig* ControlRig : ControlRigs)
		{
			TArray<FName> SelectedControls = ControlRig->CurrentControlSelection();
			if (SelectedControls.Num() > 0)
			{
				ControlRig->Modify();
				for (const FName& Name : SelectedControls)
				{
					FTransform Transform = UControlRigSequencerEditorLibrary::GetControlRigWorldTransform(LevelSequence, ControlRig, Name, FrameNumber,
						ESequenceTimeUnit::TickResolution);

					FControlRigSelectionDuringDrag ControlDrag;
					ControlDrag.LevelSequence = LevelSequence;
					ControlDrag.ControlName = Name;
					ControlDrag.ControlRig = ControlRig;
					ControlDrag.CurrentFrame = FrameNumber;
					ControlDrag.CurrentTransform = Transform;
					ControlRigDrags.Add(ControlDrag);
				}
			}
		}
	}

	StartDragTransform = Proxy->GetTransform();
	bGizmoBeingDragged = true;
	bManipulatorMadeChange = false;
	GizmoTransform = StartDragTransform;
}

void USequencerPivotTool::GizmoTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	if (!bGizmoBeingDragged)
	{
		return;
	}
	GizmoTransform = Transform;
	FTransform Diff = Transform.GetRelativeTransform(StartDragTransform);
	if (Diff.GetRotation().IsIdentity(1e-4f) == false)
	{
		const bool bSetKey = false;
		bManipulatorMadeChange = true;
		for (FControlRigSelectionDuringDrag& ControlDrag : ControlRigDrags)
		{
			FVector LocDiff = ControlDrag.CurrentTransform.GetLocation() - Transform.GetLocation();
			if (LocDiff.IsNearlyZero(1e-4f) == false)
			{
				FVector RotatedDiff = Diff.GetRotation().RotateVector(LocDiff);
				FVector NewLocation = Transform.GetLocation() + RotatedDiff;
				ControlDrag.CurrentTransform.SetLocation(NewLocation);
				UControlRigSequencerEditorLibrary::SetControlRigWorldTransform(ControlDrag.LevelSequence, ControlDrag.ControlRig, ControlDrag.ControlName, 
					ControlDrag.CurrentFrame,ControlDrag.CurrentTransform, ESequenceTimeUnit::TickResolution, bSetKey);
			}
		}
	}
	
	StartDragTransform = Transform;
	UpdateGizmoTransform();

}

void USequencerPivotTool::GizmoTransformEnded(UTransformProxy* Proxy)
{

	if (bManipulatorMadeChange && TransactionIndex != INDEX_NONE)
	{
		//GEditor->EndTransaction();
	}
	else if (TransactionIndex != INDEX_NONE)
	{
	//	GEditor->CancelTransaction(TransactionIndex);
	}
	bGizmoBeingDragged = false;
	bManipulatorMadeChange = false;
	UpdateGizmoTransform();
	SavePivotTransforms();
}

void USequencerPivotTool::SavePivotTransforms()
{
	for (FControlRigMappings& LastObject : LastSelectedObjects)
	{
		FControlRigMappings& Mappings = SavedPivotLocations.FindOrAdd(LastObject.ControlRig);
		Mappings.ControlRig = LastObject.ControlRig;
		for (TPair<FName, FTransform>& Pair : LastObject.PivotTransforms)
		{
			FTransform& Transform = Mappings.PivotTransforms.FindOrAdd(Pair.Key);
			Transform = GizmoTransform;
		}
	}
}

//currently always visibile
void USequencerPivotTool::UpdateGizmoVisibility()
{
	if (TransformGizmo)
	{
		TransformGizmo->SetVisibility(true);
	}
}

void USequencerPivotTool::UpdateGizmoTransform()
{
	if (!TransformGizmo)
	{
		return;
	}

	TransformGizmo->ReinitializeGizmoTransform(GizmoTransform);

}

FInputRayHit USequencerPivotTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	//OnUpdateModifierState is not called yet so use modifier keys unfortunately
	FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();
	bPickingPivotLocation = KeyState.IsControlDown();
	if (bPickingPivotLocation == true)
	{
		FVector Temp;
		FInputRayHit Result = FindRayHit(ClickPos.WorldRay, Temp);
		return Result;
	}
	return FInputRayHit();
}

void USequencerPivotTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (bPickingPivotLocation)
	{
		FVector HitLocation;
		FInputRayHit Result = FindRayHit(ClickPos.WorldRay, HitLocation);
		if (Result.bHit)
		{
			GizmoTransform.SetLocation(HitLocation);
			UpdateGizmoTransform();
		}
	}
}

FInputRayHit USequencerPivotTool::FindRayHit(const FRay& WorldRay, FVector& HitPos)
{
	// trace a ray into the World
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	bool bHitWorld = TargetWorld->LineTraceSingleByObjectType(Result, WorldRay.Origin, WorldRay.PointAt(999999), QueryParams);
	if (bHitWorld)
	{
		HitPos = Result.ImpactPoint;
		return FInputRayHit(Result.Distance);
	}
	return FInputRayHit();
}



void USequencerPivotTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	// if the user updated any of the property fields, update the distance
	//UpdateDistance();
}

#include "HitProxies.h"


void USequencerPivotTool::Render(IToolsContextRenderAPI* RenderAPI)
{

	if (bPickingPivotLocation == false )
	{
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

		const bool bHitTesting = PDI && PDI->IsHitTesting();
		const float KeySize = 20.0f;
		const FLinearColor Color = FLinearColor(1.0f, 0.0f, 0.0f);
		if (bHitTesting)
		{
			PDI->SetHitProxy(new HSequencerPivotProxy());
		}

		PDI->DrawPoint(GizmoTransform.GetLocation(), Color, KeySize, SDPG_MAX);

		if (bHitTesting)
		{
			PDI->SetHitProxy(nullptr);
		}
	}
}




#undef LOCTEXT_NAMESPACE
