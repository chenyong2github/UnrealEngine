// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditMode.h"
#include "ControlRigEditModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "SControlRigEditModeTools.h"
#include "Algo/Transform.h"
#include "ControlRig.h"
#include "HitProxies.h"
#include "ControlRigEditModeSettings.h"
#include "ISequencer.h"
#include "SequencerSettings.h"
#include "Sequencer/ControlRigSequence.h"
#include "Sequencer/ControlRigBindingTemplate.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "MovieScene.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "Components/SkeletalMeshComponent.h"
#include "ControlRigEditModeCommands.h"
#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"
#include "ISequencerModule.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "ControlRigEditorModule.h"
#include "Constraint.h"
#include "EngineUtils.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "IControlRigObjectBinding.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Drawing/ControlRigDrawInterface.h"
#include "ControlRigBlueprint.h"
#include "ControlRigController.h"
#include "ControlRigGizmoActor.h"
#include "DefaultControlRigManipulationLayer.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SEditorViewport.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

FName FControlRigEditMode::ModeName("EditMode.ControlRig");

#define LOCTEXT_NAMESPACE "ControlRigEditMode"

/** The different parts of a transform that manipulators can support */
enum class ETransformComponent
{
	None,

	Rotation,

	Translation,

	Scale
};

FControlRigEditMode::FControlRigEditMode()
	: bIsTransacting(false)
	, bManipulatorMadeChange(false)
	, bSelecting(false)
	, PivotTransform(FTransform::Identity)
	, bRecreateManipulationLayerRequired(false)
	, ManipulationLayer(nullptr)
	, CurrentViewportClient(nullptr)
{
	Settings = NewObject<UControlRigEditModeSettings>(GetTransientPackage(), TEXT("Settings"));
	
	// @todo: thinking of removing this, but for now leaving it. 
	// it indicates, execution is stopped, and you can modify default bone transform or controls if set to be true
	bEnableRigElementDefaultPoseEditing = false;

	CommandBindings = MakeShareable(new FUICommandList);
	BindCommands();

#if WITH_EDITOR
	GEditor->OnObjectsReplaced().AddRaw(this, &FControlRigEditMode::OnObjectsReplaced);
#endif
}

FControlRigEditMode::~FControlRigEditMode()
{
	CommandBindings = nullptr;

#if WITH_EDITOR
	GEditor->OnObjectsReplaced().RemoveAll(this);
#endif
}

void FControlRigEditMode::SetSequencer(TSharedPtr<ISequencer> InSequencer)
{
	static bool bRecursionGuard = false;
	if (!bRecursionGuard)
	{
		TGuardValue<bool> ScopeGuard(bRecursionGuard, true);

		Settings->Sequence = nullptr;

		WeakSequencer = InSequencer;
		if(UsesToolkits())
		{
			StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetSequencer(InSequencer);
		}

		if (InSequencer.IsValid())
		{
			if (UControlRigSequence* Sequence = ExactCast<UControlRigSequence>(InSequencer->GetFocusedMovieSceneSequence()))
			{
				Settings->Sequence = Sequence;
				ReBindToActor();
			}
		}
	}
}

void FControlRigEditMode::SetObjects(const TWeakObjectPtr<>& InSelectedObject, const FGuid& InObjectBinding, UObject* BindingObject)
{
	WeakControlRigEditing = Cast<UControlRig>(InSelectedObject.Get());
	ControlRigGuid = InObjectBinding;

	// if we get binding object, set it to control rig binding object
	if (BindingObject)
	{
		if (UControlRig* ControlRig = WeakControlRigEditing.Get())
		{
			if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
			{
				if (ObjectBinding->GetBoundObject() == nullptr)
				{
					ObjectBinding->BindToObject(BindingObject);
				}
			}
		}

	}

	SetObjects_Internal();
}

void FControlRigEditMode::SetObjects_Internal()
{
	TArray<TWeakObjectPtr<>> SelectedObjects;
	if(IsInLevelEditor())
	{
		SelectedObjects.Add(Settings);
	}

	if(WeakControlRigEditing.IsValid())
	{
		SelectedObjects.Add(WeakControlRigEditing);
		WeakControlRigEditing.Get()->DrawInterface = &DrawInterface;
	}

	if(UsesToolkits())
	{
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetDetailsObjects(SelectedObjects);
	}

	// create default manipulation layer
	RecreateManipulationLayer();
	HandleSelectionChanged();
}

void FControlRigEditMode::HandleBindToActor(AActor* InActor, bool bFocus)
{
	static bool bRecursionGuard = false;
	if (!bRecursionGuard)
	{
		TGuardValue<bool> ScopeGuard(bRecursionGuard, true);

		if(IsInLevelEditor())
		{
			FControlRigBindingTemplate::SetObjectBinding(InActor);
		}

		if (WeakSequencer.IsValid())
		{
			TSharedRef<ISequencer> Sequencer = WeakSequencer.Pin().ToSharedRef();

			// Modify the sequence
			if (UControlRigSequence* Sequence = ExactCast<UControlRigSequence>(Sequencer->GetFocusedMovieSceneSequence()))
			{
				Sequence->Modify(false);

				// Also modify the binding tracks in the sequence, so bindings get regenerated to this actor
				UMovieScene* MovieScene = Sequence->GetMovieScene();
				for (UMovieSceneSection* Section : MovieScene->GetAllSections())
				{
					if (UMovieSceneSpawnSection* SpawnSection = Cast<UMovieSceneSpawnSection>(Section))
					{
						SpawnSection->TryModify(false);
					}
				}

				// now notify the sequence (will rebind when it re-evaluates
				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChangedRefreshImmediately);

				// we tick first
				ManipulationLayer->TickManipulatableObjects(0.f);

				// Now re-display our objects in the details panel (they may have changed)
				if (MovieScene->GetSpawnableCount() > 0)
				{
					FGuid SpawnableGuid = MovieScene->GetSpawnable(0).GetGuid();
					TWeakObjectPtr<> BoundObject = Sequencer->FindSpawnedObjectOrTemplate(SpawnableGuid);
					SetObjects(BoundObject, SpawnableGuid, nullptr);
				}
			}

			if (bFocus && InActor && IsInLevelEditor())
			{
				const bool bNotifySelectionChanged = false;
				const bool bDeselectBSP = true;
				const bool bWarnAboutTooManyActors = false;
				const bool bSelectEvenIfHidden = true;

				// Select & focus the actor
				GEditor->GetSelectedActors()->Modify();
				GEditor->GetSelectedActors()->BeginBatchSelectOperation();
				GEditor->SelectNone(bNotifySelectionChanged, bDeselectBSP, bWarnAboutTooManyActors);
				GEditor->SelectActor(InActor, true, bNotifySelectionChanged, bSelectEvenIfHidden);
				GEditor->Exec(InActor->GetWorld(), TEXT("CAMERA ALIGN ACTIVEVIEWPORTONLY"));
				GEditor->SelectNone(bNotifySelectionChanged, bDeselectBSP, bWarnAboutTooManyActors);
				GEditor->GetSelectedActors()->EndBatchSelectOperation();
			}
		}
	}
}

void FControlRigEditMode::ReBindToActor()
{
	if (Settings->Actor.IsValid())
	{
		HandleBindToActor(Settings->Actor.Get(), false);
	}
}

bool FControlRigEditMode::UsesToolkits() const
{
	return IsInLevelEditor();
}

void FControlRigEditMode::Enter()
{
	// Call parent implementation
	FEdMode::Enter();

	if(UsesToolkits())
	{
		if (!Toolkit.IsValid())
		{
			Toolkit = MakeShareable(new FControlRigEditModeToolkit(*this));
		}

		Toolkit->Init(Owner->GetToolkitHost());
	}

	SetObjects_Internal();
}

void FControlRigEditMode::Exit()
{
	if (bIsTransacting)
	{
		if (ManipulationLayer)
		{
			ManipulationLayer->EndTransaction();
		}

		if (GEditor)
		{
			GEditor->EndTransaction();
		}

		bIsTransacting = false;
		bManipulatorMadeChange = false;
	}

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
	}

	if (ManipulationLayer)
	{
		ManipulationLayer->DestroyLayer();
		ManipulationLayer = nullptr;
	}

	// Call parent implementation
	FEdMode::Exit();
}

void FControlRigEditMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	ViewportClient->Invalidate();

	RecalcPivotTransform();

	if (bRecreateManipulationLayerRequired)
	{
		RecreateManipulationLayer();

		for (const FRigElementKey& SelectedKey : SelectedRigElements)
		{
			if (SelectedKey.Type == ERigElementType::Control)
			{
				AControlRigGizmoActor* GizmoActor = GetGizmoFromControlName(SelectedKey.Name);
				if (GizmoActor)
				{
					GizmoActor->SetSelected(true);
				}
			}
		}
		HandleSelectionChanged();
		bRecreateManipulationLayerRequired = false;
	}

	// If we have detached from sequencer, unbind the settings UI
	if (!WeakSequencer.IsValid() && Settings->Sequence != nullptr)
	{
		Settings->Sequence = nullptr;
		RefreshObjects();
	}
}

void FControlRigEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	bool bRender = true;
	if (WeakSequencer.IsValid())
	{
		bRender = WeakSequencer.Pin()->GetPlaybackStatus() != EMovieScenePlayerStatus::Playing || Settings->bShowManipulatorsDuringPlayback;
	}

	if (!WeakControlRigEditing.IsValid())
	{
		DrawInterface.DrawInstructions.Reset();
		return;
	}

	// Force off manipulators if hide flag is set
	if (Settings->bHideManipulators)
	{
		bRender = false;
	}

	FTransform ComponentTransform = (ManipulationLayer)? ManipulationLayer->GetSkeletalMeshComponentTransform() : FTransform::Identity;
	if (bRender)
	{
		if (Settings->bDisplayHierarchy)
		{
			UControlRig* ControlRig = WeakControlRigEditing.Get();
			// each base hierarchy Bone
			const FRigBoneHierarchy& BaseHierarchy = ControlRig->GetBoneHierarchy();
			for (int32 BoneIndex = 0; BoneIndex < BaseHierarchy.Num(); ++BoneIndex)
			{
				const FRigBone& CurrentBone = BaseHierarchy[BoneIndex];
				const FTransform Transform = BaseHierarchy.GetGlobalTransform(BoneIndex);

				if (CurrentBone.ParentIndex != INDEX_NONE)
				{
					const FTransform ParentTransform = BaseHierarchy.GetGlobalTransform(CurrentBone.ParentIndex);

					PDI->DrawLine(ComponentTransform.TransformPosition(Transform.GetLocation()),ComponentTransform.TransformPosition(ParentTransform.GetLocation()), FLinearColor::White, SDPG_Foreground);
				}

				PDI->DrawPoint(ComponentTransform.TransformPosition(Transform.GetLocation()), FLinearColor::White, 5.0f, SDPG_Foreground);
			}
		}

		if (Settings->bDisplayAxesOnSelection && Settings->AxisScale > SMALL_NUMBER)
		{
			UControlRig* ControlRig = WeakControlRigEditing.Get();
			const FRigHierarchyContainer* Hierarchy = ControlRig->GetHierarchy();
			const float Scale = Settings->AxisScale;

			for (const FRigElementKey& SelectedElement : SelectedRigElements)
			{
				FTransform ElementTransform = Hierarchy->GetGlobalTransform(SelectedElement);
				ElementTransform = ElementTransform * ComponentTransform;

				PDI->DrawLine(ElementTransform.GetTranslation(), ElementTransform.TransformPosition(FVector(Scale, 0.f, 0.f)), FLinearColor::Red, SDPG_Foreground);
				PDI->DrawLine(ElementTransform.GetTranslation(), ElementTransform.TransformPosition(FVector(0.f, Scale, 0.f)), FLinearColor::Green, SDPG_Foreground);
				PDI->DrawLine(ElementTransform.GetTranslation(), ElementTransform.TransformPosition(FVector(0.f, 0.f, Scale)), FLinearColor::Blue, SDPG_Foreground);
			}
		}
	}


	for (const FControlRigDrawInterface::FDrawIntruction& Instruction : DrawInterface.DrawInstructions)
	{
		if (Instruction.Positions.Num() == 0)
		{
			continue;
		}
		switch (Instruction.DrawType)
		{
			case FControlRigDrawInterface::EDrawType_Point:
			{
				for (const FVector& Point : Instruction.Positions)
				{
					PDI->DrawPoint(ComponentTransform.TransformPosition(Point), Instruction.Color, Instruction.Thickness, SDPG_Foreground);
				}
				break;
			}
			case FControlRigDrawInterface::EDrawType_Lines:
			{
				const TArray<FVector>& Points = Instruction.Positions;
				for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex += 2)
				{
					PDI->DrawLine(ComponentTransform.TransformPosition(Points[PointIndex]), ComponentTransform.TransformPosition(Points[PointIndex+1]), Instruction.Color, SDPG_Foreground, Instruction.Thickness);
				}
				break;
			}
			case FControlRigDrawInterface::EDrawType_LineStrip:
			{
				const TArray<FVector>& Points = Instruction.Positions;
				for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex++)
				{
					PDI->DrawLine(ComponentTransform.TransformPosition(Points[PointIndex]), ComponentTransform.TransformPosition(Points[PointIndex + 1]), Instruction.Color, SDPG_Foreground, Instruction.Thickness);
				}
				break;
			}
		}
	}
	DrawInterface.DrawInstructions.Reset();
}

bool FControlRigEditMode::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent)
{
	if (InEvent != IE_Released)
	{
		TGuardValue<FEditorViewportClient*> ViewportGuard(CurrentViewportClient, InViewportClient);

		FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();
		if (CommandBindings->ProcessCommandBindings(InKey, KeyState, (InEvent == IE_Repeat)))
		{
			return true;
		}
	}

	return FEdMode::InputKey(InViewportClient, InViewport, InKey, InEvent);
}

bool FControlRigEditMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (bIsTransacting)
	{
		if (bManipulatorMadeChange)
		{
			// One final notify of our manipulators to make sure the property is keyed
			for (AControlRigGizmoActor* GizmoActor : GizmoActors)
			{
				if (GizmoActor->IsManipulating())
				{
					GizmoActor->SetManipulating(false);
				}
			}
		}

		ManipulationLayer->EndTransaction();
		GEditor->EndTransaction();
		bIsTransacting = false;
		bManipulatorMadeChange = false;
		return true;
	}

	bManipulatorMadeChange = false;

	return false;
}

bool FControlRigEditMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (!bIsTransacting)
	{
		GEditor->BeginTransaction(LOCTEXT("MoveControlTransaction", "Move Control"));

		ManipulationLayer->BeginTransaction();
		for (AControlRigGizmoActor* GizmoActor : GizmoActors)
		{
			GizmoActor->SetManipulating(true);
		}

		bIsTransacting = true;
		bManipulatorMadeChange = false;

		return bIsTransacting;
	}

	return false;
}

bool FControlRigEditMode::UsesTransformWidget() const
{
	for (const AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		if (GizmoActor->IsSelected())
		{
			return true;
		}
	}

	if (AreRigElementSelectedAndMovable())
	{
		return true;
	}

	return FEdMode::UsesTransformWidget();
}

bool FControlRigEditMode::UsesTransformWidget(FWidget::EWidgetMode CheckMode) const
{
	for (const AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		if (GizmoActor->IsSelected())
		{
			return ManipulationLayer->ModeSupportedByGizmoActor(GizmoActor, CheckMode);
		}
	}

	if (AreRigElementSelectedAndMovable())
	{
		return true;
	}

	return FEdMode::UsesTransformWidget(CheckMode);
}

FVector FControlRigEditMode::GetWidgetLocation() const
{
	if (AreRigElementSelectedAndMovable())
	{
		FTransform ComponentTransform = ManipulationLayer->GetSkeletalMeshComponentTransform();
		return ComponentTransform.TransformPosition(PivotTransform.GetLocation());
	}

	return FEdMode::GetWidgetLocation();
}

bool FControlRigEditMode::GetCustomDrawingCoordinateSystem(FMatrix& OutMatrix, void* InData)
{
	if (AreRigElementSelectedAndMovable())
	{
		OutMatrix = PivotTransform.ToMatrixNoScale().RemoveTranslation();
		return true;
	}

	return false;
}

bool FControlRigEditMode::GetCustomInputCoordinateSystem(FMatrix& OutMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(OutMatrix, InData);
}

bool FControlRigEditMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy *HitProxy, const FViewportClick &Click)
{
	if(HActor* ActorHitProxy = HitProxyCast<HActor>(HitProxy))
	{
		if(ActorHitProxy->Actor)
		{
			if (ActorHitProxy->Actor->IsA<AControlRigGizmoActor>())
			{
				AControlRigGizmoActor* GizmoActor = CastChecked<AControlRigGizmoActor>(ActorHitProxy->Actor);
				const FControlData* ControlData = ManipulationLayer->GetControlDataFromGizmo(GizmoActor);

				if (ControlData)
				{
					const FName& ControlName = ControlData->ControlName;
					if (Click.IsShiftDown() || Click.IsControlDown())
					{
						SetRigElementSelection(ERigElementType::Control, ControlName, true);
					}
					else
					{
						ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::Control));
						SetRigElementSelection(ERigElementType::Control, ControlName, true);
					}
				}
			}
		}

		// for now we show this menu all the time if body is selected
		// if we want some global menu, we'll have to move this
		if (Click.GetKey() == EKeys::RightMouseButton)
		{
			OpenContextMenu(InViewportClient);
		}

		return true;
	}

	// for now we show this menu all the time if body is selected
	// if we want some global menu, we'll have to move this
	if (Click.GetKey() == EKeys::RightMouseButton)
	{
		OpenContextMenu(InViewportClient);
		return true;
	}

	// clear selected controls
	ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::All));

	// If we are animating then swallow clicks so we dont select things other than controls
	if(WeakSequencer.IsValid() && WeakSequencer.Pin()->GetFocusedMovieSceneSequence()->IsA<UControlRigSequence>())
	{
		return true;
	}

	return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
}

void FControlRigEditMode::OpenContextMenu(FEditorViewportClient* InViewportClient)
{
	TSharedPtr<FUICommandList> Commands = CommandBindings;
	if (OnContextMenuCommandsDelegate.IsBound())
	{
		Commands = OnContextMenuCommandsDelegate.Execute();
	}

	if (OnContextMenuDelegate.IsBound())
	{
		FMenuBuilder MenuBuilder(true, Commands);
		OnContextMenuDelegate.Execute(MenuBuilder);

		TSharedPtr<SWidget> MenuWidget = MenuBuilder.MakeWidget();
		TSharedPtr<SWidget> ParentWidget = InViewportClient->GetEditorViewportWidget();

		if (MenuWidget.IsValid() && ParentWidget.IsValid())
		{
			const FVector2D MouseCursorLocation = FSlateApplication::Get().GetCursorPos();

			FSlateApplication::Get().PushMenu(
				ParentWidget.ToSharedRef(),
				FWidgetPath(),
				MenuWidget.ToSharedRef(),
				MouseCursorLocation,
				FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
			);
		}
	}
}

bool FControlRigEditMode::IntersectSelect(bool InSelect, const TFunctionRef<bool(const AControlRigGizmoActor*, const FTransform&)>& Intersects)
{
	FTransform ComponentTransform = ManipulationLayer->GetSkeletalMeshComponentTransform();

	bool bSelected = false;
	for (AControlRigGizmoActor* GizmoActor: GizmoActors)
	{
		const FTransform ControlTransform = GizmoActor->GetGlobalTransform() * ComponentTransform;
		if (Intersects(GizmoActor, ControlTransform))
		{
			const FControlData* ControlData = ManipulationLayer->GetControlDataFromGizmo(GizmoActor);
			if (ControlData)
			{
				SetRigElementSelection(ERigElementType::Control, ControlData->ControlName, InSelect);
				bSelected = true;
			}
		}
	}

	return bSelected;
}

bool FControlRigEditMode::BoxSelect(FBox& InBox, bool InSelect)
{
	bool bIntersects = IntersectSelect(InSelect, [&](const AControlRigGizmoActor* GizmoActor, const FTransform& Transform)
	{ 
		if(GizmoActor != nullptr)
		{
			FBox Bounds = GizmoActor->GetComponentsBoundingBox(true);
			Bounds = Bounds.TransformBy(Transform);
			return InBox.Intersect(Bounds);
		}
		return false;
	});

	if (bIntersects)
	{
		return true;
	}

	return FEdMode::BoxSelect(InBox, InSelect);
}

bool FControlRigEditMode::FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect)
{
	bool bIntersects = IntersectSelect(InSelect, [&](const AControlRigGizmoActor* GizmoActor, const FTransform& Transform)
	{
		if(GizmoActor != nullptr)
		{
			FBox Bounds = GizmoActor->GetComponentsBoundingBox(true);
			Bounds = Bounds.TransformBy(Transform);
			return InFrustum.IntersectBox(Bounds.GetCenter(), Bounds.GetExtent());
		}
		return false;
	});

	if (bIntersects)
	{
		return true;
	}

	return FEdMode::FrustumSelect(InFrustum, InViewportClient, InSelect);
}

void FControlRigEditMode::SelectNone()
{
	ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::All));

	FEdMode::SelectNone();
}

bool FControlRigEditMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	FVector Drag = InDrag;
	FRotator Rot = InRot;
	FVector Scale = InScale;

	const bool bCtrlDown = InViewport->KeyState(EKeys::LeftControl) || InViewport->KeyState(EKeys::RightControl);
	const bool bShiftDown = InViewport->KeyState(EKeys::LeftShift) || InViewport->KeyState(EKeys::RightShift);
	const bool bAltDown = InViewport->KeyState(EKeys::LeftAlt) || InViewport->KeyState(EKeys::RightAlt);
	const bool bMouseButtonDown = InViewport->KeyState(EKeys::LeftMouseButton);

	const FWidget::EWidgetMode WidgetMode = InViewportClient->GetWidgetMode();
	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	const ECoordSystem CoordSystem = InViewportClient->GetWidgetCoordSystemSpace();

	if (bIsTransacting && bMouseButtonDown && !bCtrlDown && !bShiftDown && !bAltDown && CurrentAxis != EAxisList::None)
	{
		const bool bDoRotation = !Rot.IsZero() && (WidgetMode == FWidget::WM_Rotate || WidgetMode == FWidget::WM_TranslateRotateZ);
		const bool bDoTranslation = !Drag.IsZero() && (WidgetMode == FWidget::WM_Translate || WidgetMode == FWidget::WM_TranslateRotateZ);
		const bool bDoScale = !Scale.IsZero() && WidgetMode == FWidget::WM_Scale;

		FTransform ComponentTransform = ManipulationLayer->GetSkeletalMeshComponentTransform();
		if (AreRigElementsSelected(FRigElementTypeHelper::ToMask(ERigElementType::Control)))
		{
			for (AControlRigGizmoActor* GizmoActor : GizmoActors)
			{
				if (GizmoActor->IsSelected())
				{
					// test local vs global
					ManipulationLayer->MoveGizmo(GizmoActor, bDoTranslation, InDrag, bDoRotation, InRot, bDoScale, InScale, ComponentTransform);
					bManipulatorMadeChange = true;
				}
			}

			RecalcPivotTransform();

			if (bManipulatorMadeChange)
			{
				ManipulationLayer->TickManipulatableObjects(0.f);
			}
			return true;
		}
		else if (AreRigElementSelectedAndMovable())
		{
			// set Bone transform
			// that will set initial Bone transform
			for (int32 Index = 0; Index < SelectedRigElements.Num(); ++Index)
			{
				const ERigElementType SelectedRigElementType = SelectedRigElements[Index].Type;

				if (SelectedRigElementType == ERigElementType::Bone || SelectedRigElementType == ERigElementType::Control)
				{
					FTransform NewWorldTransform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false) * ComponentTransform;
					bool bTransformChanged = false;
					if (bDoRotation)
					{
						FQuat CurrentRotation = NewWorldTransform.GetRotation();
						CurrentRotation = (Rot.Quaternion() * CurrentRotation);
						NewWorldTransform.SetRotation(CurrentRotation);
						bTransformChanged = true;
					}

					if (bDoTranslation)
					{
						FVector CurrentLocation = NewWorldTransform.GetLocation();
						CurrentLocation = CurrentLocation + Drag;
						NewWorldTransform.SetLocation(CurrentLocation);
						bTransformChanged = true;
					}

					if (bDoScale)
					{
						FVector CurrentScale = NewWorldTransform.GetScale3D();
						CurrentScale = CurrentScale + Scale;
						NewWorldTransform.SetScale3D(CurrentScale);
						bTransformChanged = true;
					}

					if (bTransformChanged)
					{
						FTransform NewComponentTransform = NewWorldTransform.GetRelativeTransform(ComponentTransform);
						OnSetRigElementTransformDelegate.Execute(SelectedRigElements[Index], NewComponentTransform, false);
						bManipulatorMadeChange = true;
					}
				}
			}

			// not sure this makes sense @rethink
			return bManipulatorMadeChange;
		}
	}
	return false;
}

bool FControlRigEditMode::ShouldDrawWidget() const
{
	if (AreRigElementSelectedAndMovable())
	{
		return true;
	}

	return FEdMode::ShouldDrawWidget();
}

bool FControlRigEditMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	if (OtherModeID == FBuiltinEditorModes::EM_Placement)
	{
		return false;
	}
	return true;
}

void FControlRigEditMode::AddReferencedObjects( FReferenceCollector& Collector )
{
	Collector.AddReferencedObject(Settings);
	Collector.AddReferencedObject(ManipulationLayer);

	for (AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		Collector.AddReferencedObject(GizmoActor);
	}
}


void FControlRigEditMode::ClearRigElementSelection(uint32 InTypes)
{
	if (!WeakControlRigEditing.IsValid())
	{
		return;
	}

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(WeakControlRigEditing->GetClass()->ClassGeneratedBy);
	if (Blueprint)
	{
		Blueprint->HierarchyContainer.ClearSelection();
	}
}

// internal private function that doesn't use guarding.
void FControlRigEditMode::SetRigElementSelectionInternal(ERigElementType Type, const FName& InRigElementName, bool bSelected)
{
	if (!WeakControlRigEditing.IsValid())
	{
		return;
	}

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(WeakControlRigEditing->GetClass()->ClassGeneratedBy);
	if (Blueprint)
	{
		Blueprint->HierarchyContainer.Select(FRigElementKey(InRigElementName, Type), bSelected);
	}
}

void FControlRigEditMode::SetRigElementSelection(ERigElementType Type, const FName& InRigElementName, bool bSelected)
{
	if (!bSelecting)
	{
		TGuardValue<bool> ReentrantGuard(bSelecting, true);

		SetRigElementSelectionInternal(Type, InRigElementName, bSelected);

		HandleSelectionChanged();
	}
}

void FControlRigEditMode::SetRigElementSelection(ERigElementType Type, const TArray<FName>& InRigElementNames, bool bSelected)
{
	if (!bSelecting)
	{
		TGuardValue<bool> ReentrantGuard(bSelecting, true);

		for (const FName& ElementName : InRigElementNames)
		{
			SetRigElementSelectionInternal(Type, ElementName, bSelected);
		}

		HandleSelectionChanged();
	}
}

bool FControlRigEditMode::AreRigElementsSelected(uint32 InTypes) const
{
	for (const FRigElementKey& Ele : SelectedRigElements)
	{
		if (FRigElementTypeHelper::DoesHave(InTypes, Ele.Type))
		{
			return true;
		}
	}

	return false;
}

int32 FControlRigEditMode::GetNumSelectedRigElements(uint32 InTypes) const
{
	if (FRigElementTypeHelper::DoesHave(InTypes, ERigElementType::All))
	{
		return SelectedRigElements.Num();
	}
	else
	{
		int32 NumSelected = 0;
		for (const FRigElementKey& Ele : SelectedRigElements)
		{
			if (FRigElementTypeHelper::DoesHave(InTypes, Ele.Type))
			{
				++NumSelected;
			}
		}

		return NumSelected;
	}

	return 0;
}

void FControlRigEditMode::HandleObjectSpawned(FGuid InObjectBinding, UObject* SpawnedObject, IMovieScenePlayer& Player)
{
	if (WeakSequencer.IsValid())
	{
		// check whether this spawned object is from our sequence
		TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
		if (Sequencer.Get() == &Player)
		{
			RefreshObjects();

			// check if the object is being displayed currently
			if (ControlRigGuid == InObjectBinding)
			{
				if (WeakControlRigEditing != Cast<UControlRig>(SpawnedObject))
				{
					WeakControlRigEditing = Cast<UControlRig>(SpawnedObject);
					SetObjects_Internal();
				}
				return;
			}

			// We didnt find an existing Guid, so set up our internal cache
			if (!ControlRigGuid.IsValid())
			{
				SetObjects(SpawnedObject, InObjectBinding, nullptr);
				if (UControlRig* ControlRig = Cast<UControlRig>(SpawnedObject))
				{
					if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
					{
						if (Settings->Actor.IsValid() && ObjectBinding->GetBoundObject() == nullptr)
						{
							ObjectBinding->BindToObject(Settings->Actor.Get());
						}
					}
				}
				ReBindToActor();
			}
		}
	}
}

void FControlRigEditMode::RefreshObjects()
{
	if (WeakSequencer.IsValid())
	{
		TSharedRef<ISequencer> Sequencer = WeakSequencer.Pin().ToSharedRef();
		UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence() ? Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene() : nullptr;
		if (MovieScene)
		{
			// check if we have an invalid Guid & invalidate Guid if so
			if (ControlRigGuid.IsValid() && MovieScene->FindSpawnable(ControlRigGuid) == nullptr)
			{
				ControlRigGuid.Invalidate();
				WeakControlRigEditing = nullptr;
			}

			SetObjects_Internal();
		}
	}
	else
	{
		WeakControlRigEditing = nullptr;
		ControlRigGuid.Invalidate();

		SetObjects_Internal();
	}
}

void FControlRigEditMode::EnableRigElementEditing(bool bEnabled)
{
	bEnableRigElementDefaultPoseEditing = bEnabled;
	RecalcPivotTransform();
}

void FControlRigEditMode::RecalcPivotTransform()
{
	PivotTransform = FTransform::Identity;

	// @todo: support bones also
	if(AreRigElementsSelected(FRigElementTypeHelper::ToMask(ERigElementType::Control)))
	{
		FTransform LastTransform = FTransform::Identity;

		// recalc coord system too
		FTransform ComponentTransform = ManipulationLayer->GetSkeletalMeshComponentTransform();

		// Use average location as pivot location
		FVector PivotLocation = FVector::ZeroVector;

		int32 NumSelectedControls = 0;
		for (const AControlRigGizmoActor* GizmoActor : GizmoActors)
		{
			if (GizmoActor->IsSelected())
			{
				LastTransform = GizmoActor->GetActorTransform().GetRelativeTransform(ComponentTransform);
				PivotLocation += LastTransform.GetLocation();
				++NumSelectedControls;
			}
		}

		PivotLocation /= (float)NumSelectedControls;
		PivotTransform.SetLocation(PivotLocation);
		
		if (NumSelectedControls == 1)
		{
			// A single Bone just uses its own transform
			FTransform WorldTransform = LastTransform * ComponentTransform;
			PivotTransform.SetRotation(WorldTransform.GetRotation());
		}
		else if (NumSelectedControls > 1)
		{
			// If we have more than one Bone selected, use the coordinate space of the component
			PivotTransform.SetRotation(ComponentTransform.GetRotation());
		}
	}
	else if (AreRigElementSelectedAndMovable())
	{
		// recalc coord system too
		FTransform ComponentTransform = ManipulationLayer->GetSkeletalMeshComponentTransform();

		// Use average location as pivot location
		FVector PivotLocation = FVector::ZeroVector;
		int32 NumSelection = 0;
		FTransform LastTransform = FTransform::Identity;
		for (int32 Index = 0; Index < SelectedRigElements.Num(); ++Index)
		{
			if (SelectedRigElements[Index].Type == ERigElementType::Control || SelectedRigElements[Index].Type == ERigElementType::Bone)
			{
				LastTransform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false);
				PivotLocation += LastTransform.GetLocation();
				++NumSelection;
			}
		}

		PivotLocation /= (float)NumSelection;
		PivotTransform.SetLocation(PivotLocation);

		if (NumSelection == 1)
		{
			// A single Bone just uses its own transform
			FTransform WorldTransform = LastTransform * ComponentTransform;
			PivotTransform.SetRotation(WorldTransform.GetRotation());
		}
		else if (NumSelection > 1)
		{
			// If we have more than one Bone selected, use the coordinate space of the component
			PivotTransform.SetRotation(ComponentTransform.GetRotation());
		}
	}
}

void FControlRigEditMode::HandleSelectionChanged()
{
	if (WeakSequencer.IsValid())
	{
		TArray<FString> SelectedNames;
		for (int32 Index=0; Index<SelectedRigElements.Num(); ++Index)
		{
			// if it's control, make sure it's selected
			if (SelectedRigElements[Index].Type == ERigElementType::Control)
			{
				SelectedNames.Add(SelectedRigElements[Index].Name.ToString());
			}
		}

		if (SelectedNames.Num() > 0)
		{
			WeakSequencer.Pin()->SelectByPropertyPaths(SelectedNames);
		}
	}

	for (AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		GizmoActor->GetComponents(PrimitiveComponents, true);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			PrimitiveComponent->PushSelectionToProxy();
		}
	}

	if (WeakSequencer.IsValid())
	{
		if (WeakSequencer.Pin()->GetSequencerSettings()->GetShowSelectedNodesOnly())
		{
			WeakSequencer.Pin()->RefreshTree();
		}
	}

	// update the pivot transform of our selected objects (they could be animating)
	RecalcPivotTransform();
}

void FControlRigEditMode::BindCommands()
{
	const FControlRigEditModeCommands& Commands = FControlRigEditModeCommands::Get();

	CommandBindings->MapAction(
		Commands.SetKey,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::SetKeysForSelectedManipulators));

	CommandBindings->MapAction(
		Commands.ToggleManipulators,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::ToggleManipulators));
}

bool FControlRigEditMode::IsControlSelected() const
{
	static uint32 TypeFlag = (uint32)ERigElementType::Control;
	return (AreRigElementsSelected(TypeFlag));
}

bool FControlRigEditMode::IsControlOrSpaceOrBoneSelected() const
{
	static uint32 TypeFlag = (uint32)ERigElementType::Bone | (uint32)ERigElementType::Control | (uint32)ERigElementType::Space;
	return (AreRigElementsSelected(TypeFlag));
}


bool FControlRigEditMode::GetRigElementGlobalTransform(const FRigElementKey& InElement, FTransform& OutGlobalTransform) const
{
	// if control, go through manipulation layer
	if (InElement.Type == ERigElementType::Control)
	{
		// this code is weird. Need to set this info in manipulation layer
		AControlRigGizmoActor* GizmoActor = GetGizmoFromControlName(InElement.Name);
		if (GizmoActor && ensure(GizmoActor->IsSelected()))
		{
			if (ManipulationLayer->GetGlobalTransform(GizmoActor, InElement.Name, OutGlobalTransform))
			{
				return true;
			}

			ensure(false);
			return false;
		}
	}
	else if (AreRigElementSelectedAndMovable())
	{
		// @tood: we often just cross ControlRig here without manipulation layer
		// should we clean this up?
		if (UControlRig* ControlRig = WeakControlRigEditing.Get())
		{
			OutGlobalTransform = ControlRig->GetHierarchy()->GetGlobalTransform(InElement);
			return true;
		}
	}

	return false;
}

void FControlRigEditMode::SetKeysForSelectedManipulators()
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();

	if (Sequencer)
	{
		for (const AControlRigGizmoActor* GizmoActor : GizmoActors)
		{
			if (GizmoActor->IsSelected())
			{
				ManipulationLayer->AddKey(GizmoActor, Sequencer);
			}
		}
	}
}

void FControlRigEditMode::ToggleManipulators()
{
	// Toggle flag (is used in drawing code)
	Settings->bHideManipulators = !Settings->bHideManipulators;
}

bool FControlRigEditMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	// Inform units of hover state
	HActor* ActorHitProxy = HitProxyCast<HActor>(Viewport->GetHitProxy(x, y));
	if(ActorHitProxy && ActorHitProxy->Actor)
	{
		if (ActorHitProxy->Actor->IsA<AControlRigGizmoActor>())
		{
			for (AControlRigGizmoActor* GizmoActor : GizmoActors)
			{
				GizmoActor->SetHovered(GizmoActor == ActorHitProxy->Actor);
			}
		}
	}

	return false;
}

bool FControlRigEditMode::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	for (AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		GizmoActor->SetHovered(false);
	}

	return false;
}

void FControlRigEditMode::RecreateManipulationLayer()
{
	if (ManipulationLayer)
	{
		ManipulationLayer->DestroyLayer();
	}

	if (UControlRig* ControlRig = WeakControlRigEditing.Get())
	{
		ManipulationLayer = NewObject<UDefaultControlRigManipulationLayer>();
		
		// create layer
		ManipulationLayer->CreateLayer();

		// default manipulation layer can support any control rig
		ManipulationLayer->AddManipulatableObject(ControlRig);

		// create gizmo actors
		GizmoActors.Reset();
		ManipulationLayer->CreateGizmoActors(GetWorld(), GizmoActors);

		USceneComponent* Component = ManipulationLayer->GetSkeletalMeshComponent();
		if (Component)
		{
			AActor* PreviewActor = Component->GetOwner();

			for (AControlRigGizmoActor* GizmoActor : GizmoActors)
			{
				// attach to preview actor, so that we can communicate via relative transfrom from the previewactor
				GizmoActor->AttachToActor(PreviewActor, FAttachmentTransformRules::KeepWorldTransform);

				TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
				GizmoActor->GetComponents(PrimitiveComponents, true);
				for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
				{
					PrimitiveComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FControlRigEditMode::GizmoSelectionOverride);
					PrimitiveComponent->PushSelectionToProxy();
				}
			}
		}
	}
}

void RequestToRecreateManipulationLayer()
{

}


FControlRigEditMode* FControlRigEditMode::GetEditModeFromWorldContext(UWorld* InWorldContext)
{
	return nullptr;
}

bool FControlRigEditMode::GizmoSelectionOverride(const UPrimitiveComponent* InComponent) const
{
	AControlRigGizmoActor* OwnerActor = Cast<AControlRigGizmoActor>(InComponent->GetOwner());
	if (OwnerActor)
	{
		// See if the actor is in a selected unit proxy
		return OwnerActor->IsSelected();
	}

	return false;
}

void FControlRigEditMode::OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap)
{
	if (WeakControlRigEditing.IsValid())
	{
		UObject* OldObject = WeakControlRigEditing.Get();
		UObject* NewObject = OldToNewInstanceMap.FindRef(OldObject);
		if (NewObject)
		{
			WeakControlRigEditing = Cast<UControlRig>(NewObject);
			WeakControlRigEditing->PostReinstanceCallback(CastChecked<UControlRig>(OldObject));
			SetObjects_Internal();
		}
	}
}

bool FControlRigEditMode::IsTransformDelegateAvailable() const
{
	return (OnGetRigElementTransformDelegate.IsBound() && OnSetRigElementTransformDelegate.IsBound());
}

bool FControlRigEditMode::AreRigElementSelectedAndMovable() const
{
	if (IsTransformDelegateAvailable())
	{
		// you can move control anytime
		if (AreRigElementsSelected(FRigElementTypeHelper::ToMask(ERigElementType::Control)))
		{
			return true;
		}

		// if bone, we want to make sure execution is stopped
		if (bEnableRigElementDefaultPoseEditing && AreRigElementsSelected(FRigElementTypeHelper::ToMask(ERigElementType::Bone)))
		{
			return true;
		}
	}

	return false;

}

void FControlRigEditMode::OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	RequestToRecreateManipulationLayer();
}

void FControlRigEditMode::OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	RequestToRecreateManipulationLayer();
}

void FControlRigEditMode::OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName)
{
	RequestToRecreateManipulationLayer();
}

void FControlRigEditMode::OnRigElementReparented(FRigHierarchyContainer* Container, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName)
{
	RequestToRecreateManipulationLayer();
}

void FControlRigEditMode::OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected)
{
	switch (InKey.Type)
	{
		case ERigElementType::Bone:
		case ERigElementType::Control:
		case ERigElementType::Space:
		case ERigElementType::Curve:
		{
			if (bSelected)
			{
				SelectedRigElements.AddUnique(InKey);
			}
			else
			{
				SelectedRigElements.Remove(InKey);
			}

			// if it's control
			if (InKey.Type == ERigElementType::Control)
			{
				// users may select gizmo and control rig units, so we have to let them go through both of them if they do
				// first go through gizmo actor
				AControlRigGizmoActor* GizmoActor = GetGizmoFromControlName(InKey.Name);
				if (GizmoActor)
				{
					GizmoActor->SetSelected(bSelected);
				}
			}

			HandleSelectionChanged();
			break;
		}
		default:
		{
			// todo
			ensureMsgf(false, TEXT("Unsupported Type of RigElement: %d"), InKey.Type);
			break;
		}
	}
}

void FControlRigEditMode::OnRigElementChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	if(!InKey) // all of them changed
	{
		RequestToRecreateManipulationLayer();
	}
}


void FControlRigEditMode::OnControlUISettingChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	RequestToRecreateManipulationLayer();
}

AControlRigGizmoActor* FControlRigEditMode::GetGizmoFromControlName(const FName& InControlName) const
{
	return ManipulationLayer->GetGizmoFromControlName(InControlName);
}

#undef LOCTEXT_NAMESPACE
