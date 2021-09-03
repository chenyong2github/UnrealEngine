// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "Sections/MovieSceneSpawnSection.h"
#include "MovieScene.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "LevelEditorViewport.h"
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
#include "ControlRigGizmoActor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SEditorViewport.h"
#include "ControlRigControlsProxy.h"
#include "ScopedTransaction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "RigVMModel/RigVMController.h"
#include "Rigs/AdditiveControlRig.h"
#include "Rigs/FKControlRig.h"
#include "ControlRigComponent.h"
#include "EngineUtils.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"

//#include "IPersonaPreviewScene.h"
//#include "Animation/DebugSkelMeshComponent.h"
//#include "Persona/Private/AnimationEditorViewportClient.h"
void UControlRigEditModeDelegateHelper::OnPoseInitialized()
{
	if (EditMode)
	{
		EditMode->OnPoseInitialized();
	}
}
void UControlRigEditModeDelegateHelper::PostPoseUpdate()
{
	if (EditMode)
	{
		EditMode->PostPoseUpdate();
	}
}

void UControlRigEditModeDelegateHelper::AddDelegates(USkeletalMeshComponent* InSkeletalMeshComponent)
{
	if (BoundComponent.IsValid())
	{
		if (BoundComponent.Get() == InSkeletalMeshComponent)
		{
			return;
		}
	}

	RemoveDelegates();

	BoundComponent = InSkeletalMeshComponent;

	if (BoundComponent.IsValid())
	{
		BoundComponent->OnAnimInitialized.AddDynamic(this, &UControlRigEditModeDelegateHelper::OnPoseInitialized);
		OnBoneTransformsFinalizedHandle = BoundComponent->RegisterOnBoneTransformsFinalizedDelegate(
			FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &UControlRigEditModeDelegateHelper::PostPoseUpdate));
	}
}

void UControlRigEditModeDelegateHelper::RemoveDelegates()
{
	if(BoundComponent.IsValid())
	{
		BoundComponent->OnAnimInitialized.RemoveAll(this);
		BoundComponent->UnregisterOnBoneTransformsFinalizedDelegate(OnBoneTransformsFinalizedHandle);
		OnBoneTransformsFinalizedHandle.Reset();
		BoundComponent = nullptr;
	}
}

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

namespace ControlRigSelectionConstants
{
	/** Distance to trace for physics bodies */
	static const float BodyTraceDistance = 100000.0f;
}

FControlRigEditMode::FControlRigEditMode()
	: InteractionScope(nullptr)
	, bManipulatorMadeChange(false)
	, bSelecting(false)
	, bSelectionChanged(false)
	, PivotTransform(FTransform::Identity)
	, bRecreateGizmosRequired(false)
	, CurrentViewportClient(nullptr)
	, bIsChangingCoordSystem(false)
{
	Settings = NewObject<UControlRigEditModeSettings>(GetTransientPackage(), NAME_None);
	ControlProxy = NewObject<UControlRigDetailPanelControlProxies>(GetTransientPackage(), NAME_None);
	ControlProxy->SetFlags(RF_Transactional);

	CommandBindings = MakeShareable(new FUICommandList);
	BindCommands();

#if WITH_EDITOR
	GEditor->OnObjectsReplaced().AddRaw(this, &FControlRigEditMode::OnObjectsReplaced);
#endif
}

FControlRigEditMode::~FControlRigEditMode()
{	
	CommandBindings = nullptr;

	GLevelEditorModeTools().OnWidgetModeChanged().RemoveAll(this);
	GLevelEditorModeTools().OnCoordSystemChanged().RemoveAll(this);

	DestroyGizmosActors();

	TArray<TWeakObjectPtr<UControlRig>> PreviousRuntimeRigs = RuntimeControlRigs;
	for (int32 PreviousRuntimeRigIndex = 0; PreviousRuntimeRigIndex < PreviousRuntimeRigs.Num(); PreviousRuntimeRigIndex++)
	{
		if (PreviousRuntimeRigs[PreviousRuntimeRigIndex].IsValid())
		{
			RemoveControlRig(PreviousRuntimeRigs[PreviousRuntimeRigIndex].Get());
		}
	}
	RuntimeControlRigs.Reset();

	if (DelegateHelper.IsValid())
	{
		DelegateHelper->RemoveDelegates();
		DelegateHelper.Reset();
	}

#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);
	}
#endif
}

void FControlRigEditMode::SetObjects(const TWeakObjectPtr<>& InSelectedObject,  UObject* BindingObject, TWeakPtr<ISequencer> InSequencer)
{
	TArray<TWeakObjectPtr<UControlRig>> PreviousRuntimeRigs = RuntimeControlRigs;
	for (int32 PreviousRuntimeRigIndex = 0; PreviousRuntimeRigIndex < PreviousRuntimeRigs.Num(); PreviousRuntimeRigIndex++)
	{
		if (PreviousRuntimeRigs[PreviousRuntimeRigIndex].IsValid())
		{
			RemoveControlRig(PreviousRuntimeRigs[PreviousRuntimeRigIndex].Get());
		}
	}
	RuntimeControlRigs.Reset();

	UControlRig* ControlRig = Cast<UControlRig>(InSelectedObject.Get());

	if (InSequencer.IsValid())
	{
		WeakSequencer = InSequencer;
	}
	// if we get binding object, set it to control rig binding object
	if (BindingObject && ControlRig)
	{
		if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
		{
			if (ObjectBinding->GetBoundObject() == nullptr)
			{
				ObjectBinding->BindToObject(BindingObject);
			}
		}

		AddControlRig(ControlRig);
	}
	else if (ControlRig)
	{
		AddControlRig(ControlRig);
	}

	SetObjects_Internal();
}

void FControlRigEditMode::SetUpDetailPanel()
{
	if (IsInLevelEditor())
	{
		TArray<TWeakObjectPtr<>> SelectedObjects;
		if (UControlRig* ControlRig = GetControlRig(true))
		{
			const TArray<UControlRigControlsProxy*>& Proxies = ControlProxy->GetSelectedProxies();
			for (UControlRigControlsProxy* Proxy : Proxies)
			{
				SelectedObjects.Add(Proxy);
			}
			SelectedObjects.Add(Settings);
		}
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetSequencer(WeakSequencer.Pin());
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetDetailsObjects(SelectedObjects);
	}
}

void FControlRigEditMode::SetObjects_Internal()
{
	for (TWeakObjectPtr<UControlRig> RuntimeRigPtr : RuntimeControlRigs)
	{
		if (UControlRig* RuntimeControlRig = RuntimeRigPtr.Get())
		{
			if (UControlRig* InteractionRig = RuntimeControlRig->InteractionRig)
			{
				InteractionRig->ControlModified().RemoveAll(this);
				InteractionRig->ControlModified().AddSP(this, &FControlRigEditMode::OnControlModified);
			}
			else
			{
				RuntimeControlRig->ControlModified().RemoveAll(this);
				RuntimeControlRig->ControlModified().AddSP(this, &FControlRigEditMode::OnControlModified);
			}
		}
	}

	// currently all the manipulatable mesh component is supposed to be same
	// if that changes, this code has to change
	if (USkeletalMeshComponent* MeshComponent = Cast<USkeletalMeshComponent>(GetHostingSceneComponent()))
	{
		DelegateHelper->AddDelegates(MeshComponent);
	}

	UControlRig* RuntimeControlRig = GetControlRig(false);
	
	UControlRig* InteractionControlRig = GetControlRig(true);

	if (UsesToolkits() && Toolkit.IsValid())
	{
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetControlRig(RuntimeControlRig);
	}

	if (InteractionControlRig)
	{
		InteractionControlRig->Hierarchy.OnElementSelected.RemoveAll(this);
		InteractionControlRig->ControlModified().RemoveAll(this);
			
		InteractionControlRig->Hierarchy.OnElementSelected.AddSP(this, &FControlRigEditMode::OnRigElementSelected);
		InteractionControlRig->ControlModified().AddSP(this, &FControlRigEditMode::OnControlModified);
	}

	if (!RuntimeControlRig)
	{
		DestroyGizmosActors();
	}
	else
	{
		// create default manipulation layer
		RecreateGizmoActors();
		HandleSelectionChanged();
	}

	SetUpDetailPanel();
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

		bIsChangingCoordSystem = false;
		if (CoordSystemPerWidgetMode.Num() < (FWidget::WM_Max))
		{
			CoordSystemPerWidgetMode.SetNum(FWidget::WM_Max);
			ECoordSystem CoordSystem = GLevelEditorModeTools().GetCoordSystem();
			for (int32 i = 0; i < FWidget::WM_Max; ++i)
			{
				CoordSystemPerWidgetMode[i] = CoordSystem;
			}
		}
	
		GLevelEditorModeTools().OnWidgetModeChanged().AddSP(this, &FControlRigEditMode::OnWidgetModeChanged);
		GLevelEditorModeTools().OnCoordSystemChanged().AddSP(this, &FControlRigEditMode::OnCoordSystemChanged);

	}

	if (DelegateHelper.IsValid())
	{
		DelegateHelper->RemoveDelegates();
		DelegateHelper.Reset();
	}

	DelegateHelper = TStrongObjectPtr<UControlRigEditModeDelegateHelper>(NewObject<UControlRigEditModeDelegateHelper>());
	DelegateHelper->EditMode = this;

	SetObjects_Internal();
}

void FControlRigEditMode::Exit()
{
	if (UControlRig* ControlRig = GetControlRig(true))
	{
		ControlRig->ClearControlSelection();
	}

	if (InteractionScope)
	{

		if (GEditor)
		{
			GEditor->EndTransaction();
		}

		delete InteractionScope;
		InteractionScope = nullptr;
		bManipulatorMadeChange = false;
	}

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
	}

	DestroyGizmosActors();

	TArray<TWeakObjectPtr<UControlRig>> PreviousRuntimeRigs = RuntimeControlRigs;
	for (int32 PreviousRuntimeRigIndex = 0; PreviousRuntimeRigIndex < PreviousRuntimeRigs.Num(); PreviousRuntimeRigIndex++)
	{
		if (PreviousRuntimeRigs[PreviousRuntimeRigIndex].IsValid())
		{
			RemoveControlRig(PreviousRuntimeRigs[PreviousRuntimeRigIndex].Get());
		}
	}
	RuntimeControlRigs.Reset();

	if (DelegateHelper.IsValid())
	{
		DelegateHelper->RemoveDelegates();
		DelegateHelper.Reset();
	}

	//clear delegates
	GLevelEditorModeTools().OnWidgetModeChanged().RemoveAll(this);
	GLevelEditorModeTools().OnCoordSystemChanged().RemoveAll(this);

	//clear proxies
	ControlProxy->RemoveAllProxies();

	//make sure the widget is reset
	ResetGizmoSize();

	// Call parent implementation
	FEdMode::Exit();
}

void FControlRigEditMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if (bSelectionChanged)
	{
		SetUpDetailPanel();
		HandleSelectionChanged();
		bSelectionChanged = false;
	}
	if (IsInLevelEditor() == false)
	{
		ViewportClient->Invalidate();
	}
	RecalcPivotTransform();

	if (bRecreateGizmosRequired)
	{
		RecreateGizmoActors();

		for (const FRigElementKey& SelectedKey : SelectedRigElements)
		{
			if (SelectedKey.Type == ERigElementType::Control)
			{
				AControlRigGizmoActor* GizmoActor = GetGizmoFromControlName(SelectedKey.Name);
				if (GizmoActor)
				{
					GizmoActor->SetSelected(true);
				}

				if (IsInLevelEditor())
				{
					if (UControlRig* ControlRig = GetControlRig(true))
					{
						FRigControl* Control = ControlRig->FindControl(SelectedKey.Name);
						if (Control)
						{
							if (!ControlRig->IsCurveControl(Control))
							{
								ControlProxy->AddProxy(SelectedKey.Name, ControlRig, Control);
							}
						}
					}
				}
			}
		}
		SetUpDetailPanel();
		HandleSelectionChanged();
		bRecreateGizmosRequired = false;
	}

	// We need to tick here since changing a bone for example
	// might have changed the transform of the Control
	{
		PostPoseUpdate();

		if (UControlRig* ControlRig = GetControlRig(true))
		{
			const FWidget::EWidgetMode CurrentWidgetMode = ViewportClient->GetWidgetMode();
			for (FRigElementKey SelectedRigElement : SelectedRigElements)
			{
				//need to loop through the gizmo actors and set widget based upon the first one
				if (AControlRigGizmoActor* GizmoActor = GetGizmoFromControlName(SelectedRigElement.Name))
				{
					if (!ModeSupportedByGizmoActor(GizmoActor, CurrentWidgetMode))
					{
						if (FRigControl* Control = ControlRig->FindControl(SelectedRigElement.Name))
						{
							switch (Control->ControlType)
							{
								case ERigControlType::Float:
								case ERigControlType::Integer:
								case ERigControlType::Vector2D:
								case ERigControlType::Position:
								case ERigControlType::Transform:
								case ERigControlType::TransformNoScale:
								case ERigControlType::EulerTransform:
								{
									ViewportClient->SetWidgetMode(FWidget::WM_Translate);
									break;
								}
								case ERigControlType::Rotator:
								{
									ViewportClient->SetWidgetMode(FWidget::WM_Rotate);
									break;
								}
								case ERigControlType::Scale:
								{
									ViewportClient->SetWidgetMode(FWidget::WM_Scale);
									break;
								}
							}
							return; //exit if we switchted
						}
					}
					else
					{
						return; //exit if we are the same
					}

				}
			}
		}
	}
}

void FControlRigEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{	
	UControlRig* ControlRig = GetControlRig(false);
	if(ControlRig == nullptr)
	{
		return;
	}

	bool bRender = !Settings->bHideManipulators;

	FTransform ComponentTransform = GetHostingSceneComponentTransform();
	if (bRender)
	{
		for (AControlRigGizmoActor* Actor : GizmoActors)
		{
			//Actor->SetActorHiddenInGame(bIsHidden);
			if (GIsEditor && Actor->GetWorld() != nullptr && !Actor->GetWorld()->IsPlayInEditor())
			{
				Actor->SetIsTemporarilyHiddenInEditor(false);
			}
		}

		if (Settings->bDisplayHierarchy)
		{
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

		if (Settings->bDisplaySpaces || ControlRig->IsSetupModeEnabled())
		{
			FRigSpaceHierarchy& SpaceHierarchy = ControlRig->GetSpaceHierarchy();

			TArray<FTransform> SpaceTransforms;
			for (const FRigSpace& Space : SpaceHierarchy)
			{
				SpaceTransforms.Add(SpaceHierarchy.GetGlobalTransform(Space.Index));
			}

			GetControlRig(true)->DrawInterface.DrawAxes(FTransform::Identity, SpaceTransforms, Settings->AxisScale);
		}

		if (Settings->bDisplayAxesOnSelection && Settings->AxisScale > SMALL_NUMBER)
		{
			if (ControlRig->GetWorld() && ControlRig->GetWorld()->IsPreviewWorld())
			{
				const FRigHierarchyContainer* Hierarchy = ControlRig->GetHierarchy();
				const float Scale = Settings->AxisScale;
				PDI->AddReserveLines(SDPG_Foreground, SelectedRigElements.Num() * 3);

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
		for (const FControlRigDrawInstruction& Instruction : GetControlRig(true)->DrawInterface)
		{
			if (!Instruction.IsValid())
			{
				continue;
			}

			FTransform InstructionTransform = Instruction.Transform * ComponentTransform;
			switch (Instruction.PrimitiveType)
			{
				case EControlRigDrawSettings::Points:
			{
				for (const FVector& Point : Instruction.Positions)
				{
						PDI->DrawPoint(InstructionTransform.TransformPosition(Point), Instruction.Color, Instruction.Thickness, SDPG_Foreground);
				}
				break;
			}
				case EControlRigDrawSettings::Lines:
			{
				const TArray<FVector>& Points = Instruction.Positions;
					PDI->AddReserveLines(SDPG_Foreground, Points.Num() / 2, false, Instruction.Thickness > SMALL_NUMBER);
				for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex += 2)
				{
						PDI->DrawLine(InstructionTransform.TransformPosition(Points[PointIndex]), InstructionTransform.TransformPosition(Points[PointIndex + 1]), Instruction.Color, SDPG_Foreground, Instruction.Thickness);
				}
				break;
			}
				case EControlRigDrawSettings::LineStrip:
			{
				const TArray<FVector>& Points = Instruction.Positions;
					PDI->AddReserveLines(SDPG_Foreground, Points.Num() - 1, false, Instruction.Thickness > SMALL_NUMBER);
				for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex++)
				{
						PDI->DrawLine(InstructionTransform.TransformPosition(Points[PointIndex]), InstructionTransform.TransformPosition(Points[PointIndex + 1]), Instruction.Color, SDPG_Foreground, Instruction.Thickness);
				}
				break;
			}

				case EControlRigDrawSettings::DynamicMesh:
			{
				FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
				MeshBuilder.AddVertices(Instruction.MeshVerts);
				MeshBuilder.AddTriangles(Instruction.MeshIndices);
				MeshBuilder.Draw(PDI, InstructionTransform.ToMatrixWithScale(), Instruction.MaterialRenderProxy, SDPG_World/*SDPG_Foreground*/);
				break;
			}

			}
		}
	}
	else
	{
		for (AControlRigGizmoActor* Actor : GizmoActors)
		{
			//Actor->SetActorHiddenInGame(bIsHidden);
			if (GIsEditor && Actor->GetWorld() != nullptr && !Actor->GetWorld()->IsPlayInEditor())
			{
				Actor->SetIsTemporarilyHiddenInEditor(true);
			}
		}
	}
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
	if (InteractionScope)
	{
		if (bManipulatorMadeChange)
		{
			bManipulatorMadeChange = false;
			GEditor->EndTransaction();
		}

		delete InteractionScope;
		InteractionScope = nullptr;

		return true;
	}

	bManipulatorMadeChange = false;

	return false;
}

bool FControlRigEditMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (InteractionScope == nullptr)
	{
	    if (UControlRig* ControlRig = GetControlRig(true))
		{
			bool bShouldModify = IsInLevelEditor();
			if (!bShouldModify)
			{
				for (const FRigElementKey& Key : SelectedRigElements)
				{
					if (Key.Type != ERigElementType::Control)
					{
						bShouldModify = true;
					}
				}
			}

			if (!IsInLevelEditor())
			{
				UObject* Blueprint = ControlRig->GetClass()->ClassGeneratedBy;
				if (Blueprint)
				{
					Blueprint->SetFlags(RF_Transactional);
					if (bShouldModify)
					{
						Blueprint->Modify();
					}
				}
			}

			ControlRig->SetFlags(RF_Transactional);
			if (bShouldModify)
			{
				ControlRig->Modify();
			}
		}

		//in level editor only transact if we have at least one control selected, in editor we only select CR stuff so always transact
		if (UControlRig* ControlRig = GetControlRig(true))
		{
			if (IsInLevelEditor())
			{
				if (AreRigElementSelectedAndMovable())
				{
					InteractionScope = new FControlRigInteractionScope(ControlRig);
				}
			}
			else
			{
				InteractionScope = new FControlRigInteractionScope(ControlRig);
			}

			bManipulatorMadeChange = false;
		}

		return InteractionScope != nullptr;
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
			return ModeSupportedByGizmoActor(GizmoActor, CheckMode);
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
		FTransform ComponentTransform = GetHostingSceneComponentTransform();
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
				if (GizmoActor->IsSelectable())
				{
					FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), IsInLevelEditor() && !GIsTransacting);
						
					const FName& ControlName = GizmoActor->ControlName;
					if (Click.IsShiftDown()) //guess we just select
					{
						SetRigElementSelection(ERigElementType::Control, ControlName, true);
					}
					else if(Click.IsControlDown()) //if ctrl we toggle selection
					{
						UControlRig* InteractionRig = GetControlRig(true);
						if (InteractionRig)
						{
							bool bIsSelected = InteractionRig->IsControlSelected(ControlName);
							SetRigElementSelection(ERigElementType::Control, ControlName, !bIsSelected);
						}
					}
					else
					{
						ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::Control));
						SetRigElementSelection(ERigElementType::Control, ControlName, true);
					}
	
					// for now we show this menu all the time if body is selected
					// if we want some global menu, we'll have to move this
					if (Click.GetKey() == EKeys::RightMouseButton)
					{
						OpenContextMenu(InViewportClient);
					}
	
					return true;
				}

				return true;
			}
			else if(UControlRig* ControlRig = GetControlRig(false))
			{ 
				//if we have an additive or fk control rig active select the control based upon the selected bone.
				UAdditiveControlRig* AdditiveControlRig = Cast<UAdditiveControlRig>(ControlRig);
				UFKControlRig* FKControlRig = Cast<UFKControlRig>(ControlRig);

				if (AdditiveControlRig || FKControlRig)
				{
					if (USkeletalMeshComponent* RigMeshComp = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
					{
						const USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(ActorHitProxy->PrimComponent);

						if (SkelComp == RigMeshComp)
						{
							FHitResult Result(1.0f);
							bool bHit = RigMeshComp->LineTraceComponent(Result, Click.GetOrigin(), Click.GetOrigin() + Click.GetDirection() * ControlRigSelectionConstants::BodyTraceDistance, FCollisionQueryParams(NAME_None, FCollisionQueryParams::GetUnknownStatId(), true));

							if (bHit)
							{
								FName ControlName(*(Result.BoneName.ToString() + TEXT("_CONTROL")));
								if (ControlRig->FindControl(ControlName))
								{
									FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), IsInLevelEditor() && !GIsTransacting);

									if (Click.IsShiftDown() || Click.IsControlDown())
									{
										SetRigElementSelection(ERigElementType::Control, ControlName, true);
									}
									else
									{
										ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::Control));
										SetRigElementSelection(ERigElementType::Control, ControlName, true);
									}
									return true;
								}
							}
						}
					}
				}
			}
		}
	}

	// for now we show this menu all the time if body is selected
	// if we want some global menu, we'll have to move this
	if (Click.GetKey() == EKeys::RightMouseButton)
	{
		OpenContextMenu(InViewportClient);
		return true;
	}

	if (Settings  &&  Settings->bOnlySelectRigControls)
	{
		return true;
	}

	FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), IsInLevelEditor() &&  !GIsTransacting);
	
	// clear selected controls
	ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::All));

	/*
	if(!InViewportClient->IsLevelEditorClient() && !InViewportClient->IsSimulateInEditorViewport())
	{
		bool bHandled = false;
		const bool bSelectingSections = GetAnimPreviewScene().AllowMeshHitProxies();

		USkeletalMeshComponent* MeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();

		if ( HitProxy )
		{
			if ( HitProxy->IsA( HPersonaBoneProxy::StaticGetType() ) )
			{			
				SetRigElementSelection(ERigElementType::Bone, static_cast<HPersonaBoneProxy*>(HitProxy)->BoneName, true);
				bHandled = true;
			}
		}
		
		if ( !bHandled && !bSelectingSections )
		{
			// Cast for phys bodies if we didn't get any hit proxies
			FHitResult Result(1.0f);
			UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();
			bool bHit = PreviewMeshComponent->LineTraceComponent(Result, Click.GetOrigin(), Click.GetOrigin() + Click.GetDirection() * 10000.0f, FCollisionQueryParams(NAME_None, FCollisionQueryParams::GetUnknownStatId(),true));
			
			if(bHit)
			{
				SetRigElementSelection(ERigElementType::Bone, Result.BoneName, true);
				bHandled = true;
			}
		}
	}
	*/
	
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
	FTransform ComponentTransform = GetHostingSceneComponentTransform();

	bool bSelected = false;
	for (AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		if (GizmoActor->IsHiddenEd())
		{
			continue;
		}

		const FTransform ControlTransform = GizmoActor->GetGlobalTransform() * ComponentTransform;
		if (Intersects(GizmoActor, ControlTransform))
		{
			SetRigElementSelection(ERigElementType::Control, GizmoActor->ControlName, InSelect);
			bSelected = true;
		}
	}

	return bSelected;
}

static FConvexVolume GetVolumeFromBox(const FBox& InBox)
{
	FConvexVolume ConvexVolume;
	ConvexVolume.Planes.Empty(6);

	ConvexVolume.Planes.Add(FPlane(FVector::LeftVector, -InBox.Min.Y));
	ConvexVolume.Planes.Add(FPlane(FVector::RightVector, InBox.Max.Y));
	ConvexVolume.Planes.Add(FPlane(FVector::UpVector, InBox.Max.Z));
	ConvexVolume.Planes.Add(FPlane(FVector::DownVector, -InBox.Min.Z));
	ConvexVolume.Planes.Add(FPlane(FVector::ForwardVector, InBox.Max.X));
	ConvexVolume.Planes.Add(FPlane(FVector::BackwardVector, -InBox.Min.X));

	ConvexVolume.Init();

	return ConvexVolume;
}

bool IntersectsBox( AActor& InActor, const FBox& InBox, FLevelEditorViewportClient* LevelViewportClient, bool bUseStrictSelection )
{
	bool bActorHitByBox = false;
	if (InActor.IsHiddenEd())
	{
		return false;
	}

	const TArray<FName>& HiddenLayers = LevelViewportClient->ViewHiddenLayers;
	bool bActorIsVisible = true;
	for ( auto Layer : InActor.Layers )
	{
		// Check the actor isn't in one of the layers hidden from this viewport.
		if( HiddenLayers.Contains( Layer ) )
		{
			return false;
		}
	}

	// Iterate over all actor components, selecting out primitive components
	for (UActorComponent* Component : InActor.GetComponents())
	{
		UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
		if (PrimitiveComponent && PrimitiveComponent->IsRegistered() && PrimitiveComponent->IsVisibleInEditor())
		{
			if (PrimitiveComponent->ComponentIsTouchingSelectionBox(InBox, LevelViewportClient->EngineShowFlags, false, bUseStrictSelection))
			{
				return true;
			}
		}
	}
	
	return false;
}

bool FControlRigEditMode::BoxSelect(FBox& InBox, bool InSelect)
{
	FLevelEditorViewportClient* LevelViewportClient = GCurrentLevelEditingViewportClient;
	const bool bStrictDragSelection = GetDefault<ULevelEditorViewportSettings>()->bStrictBoxSelection;

	FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), IsInLevelEditor() && !GIsTransacting);
	const bool bShiftDown = LevelViewportClient->Viewport->KeyState(EKeys::LeftShift) || LevelViewportClient->Viewport->KeyState(EKeys::RightShift);
	if (!bShiftDown)
	{
		ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::Control));
	}

	// Select all actors that are within the selection box area.  Be aware that certain modes do special processing below.	
	bool bSomethingSelected = false;
	UWorld* IteratorWorld = GWorld;
	for( FActorIterator It(IteratorWorld); It; ++It )
	{
		AActor* Actor = *It;

		if (!Actor->IsA<AControlRigGizmoActor>())
		{
			continue;
		}

		AControlRigGizmoActor* GizmoActor = CastChecked<AControlRigGizmoActor>(Actor);
		if (!GizmoActor->IsSelectable())
		{
			continue;
		}

		if (IntersectsBox(*Actor, InBox, LevelViewportClient, bStrictDragSelection))
		{
			bSomethingSelected = true;
			const FName& ControlName = GizmoActor->ControlName;
			SetRigElementSelection(ERigElementType::Control, ControlName, true);

			if (bShiftDown)
			{
			}
			else
			{
				SetRigElementSelection(ERigElementType::Control, ControlName, true);
			}
		}
	}
	if (bSomethingSelected == true)
	{
		return true;
	}
	
	ScopedTransaction.Cancel();
	return FEdMode::BoxSelect(InBox, InSelect);
}

bool FControlRigEditMode::FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect)
{
	FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), IsInLevelEditor() && !GIsTransacting);
	bool bSomethingSelected(false);
	const bool bShiftDown = InViewportClient->Viewport->KeyState(EKeys::LeftShift) || InViewportClient->Viewport->KeyState(EKeys::RightShift);
	if (!bShiftDown)
	{
		ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::Control));
	}

	for (AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		for (UActorComponent* Component : GizmoActor->GetComponents())
		{
			UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
			if (PrimitiveComponent && PrimitiveComponent->IsRegistered() && PrimitiveComponent->IsVisibleInEditor())
			{
				if (PrimitiveComponent->ComponentIsTouchingSelectionFrustum(InFrustum, InViewportClient->EngineShowFlags, false /*only bsp*/, false/*encompass entire*/))
				{
					if (GizmoActor->IsSelectable())
					{
						bSomethingSelected = true;
						const FName& ControlName = GizmoActor->ControlName;
						SetRigElementSelection(ERigElementType::Control, ControlName, true);
					}
				}
			}
		}
	}
	if (bSomethingSelected == true)
	{
		return true;
	}
	ScopedTransaction.Cancel();
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
	//bAltDown We don't care about if it is down we still want to move and not clone.
	const bool bMouseButtonDown = InViewport->KeyState(EKeys::LeftMouseButton);

	const FWidget::EWidgetMode WidgetMode = InViewportClient->GetWidgetMode();
	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	const ECoordSystem CoordSystem = InViewportClient->GetWidgetCoordSystemSpace();

	if (InteractionScope != nullptr && bMouseButtonDown && !bCtrlDown && !bShiftDown && CurrentAxis != EAxisList::None)
	{
		const bool bDoRotation = !Rot.IsZero() && (WidgetMode == FWidget::WM_Rotate || WidgetMode == FWidget::WM_TranslateRotateZ);
		const bool bDoTranslation = !Drag.IsZero() && (WidgetMode == FWidget::WM_Translate || WidgetMode == FWidget::WM_TranslateRotateZ);
		const bool bDoScale = !Scale.IsZero() && WidgetMode == FWidget::WM_Scale;

		if (AreRigElementsSelected(FRigElementTypeHelper::ToMask(ERigElementType::Control)))
		{
			FTransform ComponentTransform = GetHostingSceneComponentTransform();
			bool bDoLocal = (CoordSystem == ECoordSystem::COORD_Local && Settings && Settings->bLocalTransformsInEachLocalSpace);
			bool bUseLocal = false;
			bool bCalcLocal = bDoLocal;
			bool bFirstTime = true;
			FTransform InOutLocal = FTransform::Identity;
			for (AControlRigGizmoActor* GizmoActor : GizmoActors)
			{
				if (GizmoActor->IsSelected())
				{
					// test local vs global
					if (bManipulatorMadeChange == false)
					{
						GEditor->BeginTransaction(LOCTEXT("MoveControlTransaction", "Move Control"));
					}
					if (bFirstTime)
					{
						bFirstTime = false;
					}
					else
					{
						if (bDoLocal)
						{
							bUseLocal = true;
							bDoLocal = false;
						}
					}

					MoveGizmo(GizmoActor, bDoTranslation, InDrag, bDoRotation, InRot, bDoScale, InScale, ComponentTransform,
						bUseLocal,bDoLocal,InOutLocal);
					bManipulatorMadeChange = true;
				}
			}

			RecalcPivotTransform();

			if (bManipulatorMadeChange)
			{
				TickManipulatableObjects(0.f);
			}
			return true;
		}
		else if (AreRigElementSelectedAndMovable())
		{
			FTransform ComponentTransform = GetHostingSceneComponentTransform();

			// set Bone transform
			// that will set initial Bone transform
			for (int32 Index = 0; Index < SelectedRigElements.Num(); ++Index)
			{
				const ERigElementType SelectedRigElementType = SelectedRigElements[Index].Type;

				if (SelectedRigElementType == ERigElementType::Control)
				{
					FTransform NewWorldTransform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false, true) * ComponentTransform;
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
						if (bManipulatorMadeChange == false)
						{
							GEditor->BeginTransaction(LOCTEXT("MoveControlTransaction", "Move Control"));
						}
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
	if (Settings)
	{
		Collector.AddReferencedObject(Settings);
	}
	if (GizmoActors.Num() > 0)
	{
		for (AControlRigGizmoActor* GizmoActor : GizmoActors)
		{
			Collector.AddReferencedObject(GizmoActor);
		}
	}
	if (ControlProxy)
	{
		Collector.AddReferencedObject(ControlProxy);
	}
}

void FControlRigEditMode::ClearRigElementSelection(uint32 InTypes)
{
	UControlRig* InteractionRig = GetControlRig(true);
	if (InteractionRig == nullptr)
	{
		return;
	}

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(InteractionRig->GetClass()->ClassGeneratedBy);
	if (Blueprint)
	{
		Blueprint->HierarchyContainer.ClearSelection();
	}
	if (IsInLevelEditor())
	{
		InteractionRig->Hierarchy.ClearSelection();
	}
}

// internal private function that doesn't use guarding.
void FControlRigEditMode::SetRigElementSelectionInternal(ERigElementType Type, const FName& InRigElementName, bool bSelected)
{
	UControlRig* InteractionRig = GetControlRig(true);
	if (InteractionRig == nullptr)
	{
		return;
	}

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(InteractionRig->GetClass()->ClassGeneratedBy);
	if (Blueprint)
	{
		Blueprint->HierarchyContainer.Select(FRigElementKey(InRigElementName, Type), bSelected);
	}
	if (IsInLevelEditor())
	{
		InteractionRig->Hierarchy.Select(FRigElementKey(InRigElementName, Type), bSelected);
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

void FControlRigEditMode::RefreshObjects()
{
	SetObjects_Internal();
}

bool FControlRigEditMode::CanRemoveFromPreviewScene(const USceneComponent* InComponent)
{
	for (AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		TInlineComponentArray<USceneComponent*> SceneComponents;
		GizmoActor->GetComponents(SceneComponents, true);
		if (SceneComponents.Contains(InComponent))
		{
			return false;
		}
	}

	// we don't need it 
	return true;
}

void FControlRigEditMode::RecalcPivotTransform()
{
	PivotTransform = FTransform::Identity;

	// @todo: support bones also
	if (AreRigElementsSelected(FRigElementTypeHelper::ToMask(ERigElementType::Control)))
	{
		FTransform LastTransform = FTransform::Identity;

		// recalc coord system too
		FTransform ComponentTransform = GetHostingSceneComponentTransform();

		// Use average location as pivot location
		FVector PivotLocation = FVector::ZeroVector;

		int32 NumSelectedControls = 0;
		for (int32 Index = 0; Index < SelectedRigElements.Num(); ++Index)
		{
			if (SelectedRigElements[Index].Type == ERigElementType::Control)
			{
				// todo?
			}
		}

		for (const AControlRigGizmoActor* GizmoActor : GizmoActors)
		{
			if (GizmoActor->IsSelected())
			{
				LastTransform = GizmoActor->GetActorTransform().GetRelativeTransform(ComponentTransform);
				PivotLocation += LastTransform.GetLocation();
				++NumSelectedControls;
				if (Settings && Settings->bLocalTransformsInEachLocalSpace) //if in local just use first actors transform
				{
					break;
				}
			}
		}

		PivotLocation /= (float)FMath::Max(1, NumSelectedControls);
		PivotTransform.SetLocation(PivotLocation);
		
		// just use last rotation
		FTransform WorldTransform = LastTransform * ComponentTransform;
		PivotTransform.SetRotation(WorldTransform.GetRotation());
	
	}
	else if (AreRigElementSelectedAndMovable())
	{
		// recalc coord system too
		FTransform ComponentTransform = GetHostingSceneComponentTransform();

		// Use average location as pivot location
		FVector PivotLocation = FVector::ZeroVector;
		int32 NumSelection = 0;
		FTransform LastTransform = FTransform::Identity;
		for (int32 Index = 0; Index < SelectedRigElements.Num(); ++Index)
		{
			if (SelectedRigElements[Index].Type == ERigElementType::Control)
			{
				LastTransform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false, true);
				PivotLocation += LastTransform.GetLocation();
				++NumSelection;
			}
		}

		PivotLocation /= (float)FMath::Max(1, NumSelection);
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

	for (AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		GizmoActor->GetComponents(PrimitiveComponents, true);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			PrimitiveComponent->PushSelectionToProxy();
		}
	}

	
	// update the pivot transform of our selected objects (they could be animating)
	RecalcPivotTransform();
}

void FControlRigEditMode::BindCommands()
{
	const FControlRigEditModeCommands& Commands = FControlRigEditModeCommands::Get();

	CommandBindings->MapAction(
		Commands.ToggleManipulators,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::ToggleManipulators));
	CommandBindings->MapAction(
		Commands.ResetTransforms,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::ResetTransforms, true));
	CommandBindings->MapAction(
		Commands.ResetAllTransforms,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::ResetTransforms, false));
	CommandBindings->MapAction(
		Commands.ClearSelection,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::ClearSelection));

	CommandBindings->MapAction(
		Commands.FrameSelection,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::FrameSelection),
		FCanExecuteAction::CreateRaw(this, &FControlRigEditMode::CanFrameSelection)
	);

	CommandBindings->MapAction(
		Commands.IncreaseGizmoSize,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::IncreaseGizmoSize));

	CommandBindings->MapAction(
		Commands.DecreaseGizmoSize,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::DecreaseGizmoSize));

	CommandBindings->MapAction(
		Commands.ResetGizmoSize,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::ResetGizmoSize));
}

bool FControlRigEditMode::IsControlSelected() const
{
	static uint32 TypeFlag = (uint32)ERigElementType::Control;
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
			OutGlobalTransform = GetGizmoTransform(GizmoActor);
			return true;
		}
	}
	else if (AreRigElementSelectedAndMovable())
	{
		// @tood: we often just cross ControlRig here without manipulation layer
		// should we clean this up?
		if (UControlRig* ControlRig = GetControlRig(true))
		{
			OutGlobalTransform = ControlRig->GetHierarchy()->GetGlobalTransform(InElement);
			return true;
		}
	}

	return false;
}

bool FControlRigEditMode::CanFrameSelection()
{
	return SelectedRigElements.Num() > 0;
	/*
	for (const FRigElementKey& SelectedKey : SelectedRigElements)
	{
		if (SelectedKey.Type == ERigElementType::Control)
		{
			return true;
		}
	}
	return false;
	*/
}

void FControlRigEditMode::ClearSelection()
{
	ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::All));
	if (GEditor)
	{
		GEditor->Exec(GetWorld(), TEXT("SELECT NONE"));
	}
}

void FControlRigEditMode::FrameSelection()
{
	if(CurrentViewportClient)
	{
		FSphere Sphere(EForceInit::ForceInit);
		if(GetCameraTarget(Sphere))
		{
			FBox Bounds(EForceInit::ForceInit);
			Bounds += Sphere.Center;
			Bounds += Sphere.Center + FVector::OneVector * Sphere.W;
			Bounds += Sphere.Center - FVector::OneVector * Sphere.W;
			CurrentViewportClient->FocusViewportOnBox(Bounds);
		}
    }

	TArray<AActor*> Actors;
	for (const FRigElementKey& SelectedKey : SelectedRigElements)
	{
		if (SelectedKey.Type == ERigElementType::Control)
		{
			AControlRigGizmoActor* GizmoActor = GetGizmoFromControlName(SelectedKey.Name);
			if (GizmoActor)
			{
				Actors.Add(GizmoActor);
			}
		}
	}

	if (Actors.Num())
	{
		TArray<UPrimitiveComponent*> SelectedComponents;
		GEditor->MoveViewportCamerasToActor(Actors, SelectedComponents, true);
	}
}


void FControlRigEditMode::IncreaseGizmoSize()
{
	Settings->GizmoScale += 0.1f;
	FEditorModeTools& ModeTools = GLevelEditorModeTools();
	ModeTools.SetWidgetScale(Settings->GizmoScale);
}

void FControlRigEditMode::DecreaseGizmoSize()
{
	Settings->GizmoScale -= 0.1f;
	FEditorModeTools& ModeTools = GLevelEditorModeTools();
	ModeTools.SetWidgetScale(Settings->GizmoScale);
}

void FControlRigEditMode::ResetGizmoSize()
{
	Settings->GizmoScale = 1.0f;
	FEditorModeTools& ModeTools = GLevelEditorModeTools();
	ModeTools.SetWidgetScale(Settings->GizmoScale);
}

void FControlRigEditMode::ToggleManipulators()
{
	// Toggle flag (is used in drawing code)
	Settings->bHideManipulators = !Settings->bHideManipulators;
}

void FControlRigEditMode::ResetTransforms(bool bSelectionOnly)
{
	if (UControlRig* ControlRig = GetControlRig(true))
	{
		TArray<FRigElementKey> ControlsToReset = SelectedRigElements;
		if (!bSelectionOnly)
		{
			ControlsToReset = ControlRig->GetHierarchy()->GetAllItems();
		}

		FScopedTransaction Transaction(LOCTEXT("HierarchyResetTransforms", "Reset Transforms"));
		for (const FRigElementKey& ControlToReset : ControlsToReset)
		{
			if (ControlToReset.Type == ERigElementType::Control)
			{
				FRigControl* Control = ControlRig->FindControl(ControlToReset.Name);
				if (Control && !Control->bIsTransientControl)
				{
					FTransform Transform = ControlRig->GetControlHierarchy().GetLocalTransform(ControlToReset.Name, ERigControlValueType::Initial);
					ControlRig->Modify();
					ControlRig->GetControlHierarchy().SetLocalTransform(ControlToReset.Name, Transform);
					ControlRig->ControlModified().Broadcast(ControlRig, *Control, EControlRigSetKey::DoNotCare);

					if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(ControlRig->GetClass()->ClassGeneratedBy))
					{
						Blueprint->HierarchyContainer.SetLocalTransform(ControlToReset, Transform);
					}
				}
			}
		}
	}
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

void FControlRigEditMode::PostUndo()
{
	UControlRig* RuntimeControlRig = GetControlRig(false);
	if (!RuntimeControlRig)
	{
		DestroyGizmosActors();
	}
}

void FControlRigEditMode::RecreateGizmoActors(const TArray<FRigElementKey>& InSelectedElements)
{
	if (ControlProxy)
	{
		ControlProxy->RemoveAllProxies();
	}

	if (UControlRig* ControlRig = GetControlRig(false))
	{
		UControlRig* InteractionRig = ControlRig->GetInteractionRig();
		InteractionRig = InteractionRig == nullptr ? ControlRig : InteractionRig;

		// create gizmo actors
		CreateGizmoActors(GetWorld());

		USceneComponent* Component = GetHostingSceneComponent();
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
		if (IsInLevelEditor())
		{
			if (ControlProxy)
			{
				ControlProxy->RecreateAllProxies(InteractionRig);
			}
		}
	}

	for (const FRigElementKey& SelectedElement : InSelectedElements)
	{
		OnRigElementSelected(nullptr, SelectedElement, true);
	}

}

FControlRigEditMode* FControlRigEditMode::GetEditModeFromWorldContext(UWorld* InWorldContext)
{
	return nullptr;
}

bool FControlRigEditMode::GizmoSelectionOverride(const UPrimitiveComponent* InComponent) const
{
    //Think we only want to do this in regular editor, in the level editor we are driving selection
	if (!IsInLevelEditor())
	{
	AControlRigGizmoActor* OwnerActor = Cast<AControlRigGizmoActor>(InComponent->GetOwner());
	if (OwnerActor)
	{
		// See if the actor is in a selected unit proxy
		return OwnerActor->IsSelected();
	}
	}

	return false;
}

void FControlRigEditMode::OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap)
{
	for (int32 RigIndex = 0; RigIndex < RuntimeControlRigs.Num(); RigIndex++)
	{
		UObject* OldObject = RuntimeControlRigs[RigIndex].Get();
		UObject* NewObject = OldToNewInstanceMap.FindRef(OldObject);
		if (NewObject)
		{
			TArray<TWeakObjectPtr<UControlRig>> PreviousRuntimeRigs = RuntimeControlRigs;
			for (int32 PreviousRuntimeRigIndex = 0; PreviousRuntimeRigIndex < PreviousRuntimeRigs.Num(); PreviousRuntimeRigIndex++)
			{
				if (PreviousRuntimeRigs[PreviousRuntimeRigIndex].IsValid())
				{
					RemoveControlRig(PreviousRuntimeRigs[PreviousRuntimeRigIndex].Get());
				}
			}
			RuntimeControlRigs.Reset();

			UControlRig* NewRig = Cast<UControlRig>(NewObject);
			AddControlRig(NewRig);

			NewRig->Initialize();

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
	if (!AreRigElementsSelected(FRigElementTypeHelper::ToMask(ERigElementType::Control)))
	{
		return false;
	}

	//when in sequencer/level we don't have that delegate so don't check.
	if (!IsInLevelEditor())
	{
		if (!IsTransformDelegateAvailable())
	{
			return false;
		}
	}

		return true;
}

void FControlRigEditMode::OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	RequestToRecreateGizmoActors();
}

void FControlRigEditMode::OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	RequestToRecreateGizmoActors();
}

void FControlRigEditMode::OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName)
{
	RequestToRecreateGizmoActors();
}

void FControlRigEditMode::OnRigElementReparented(FRigHierarchyContainer* Container, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName)
{
	RequestToRecreateGizmoActors();
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
				FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), IsInLevelEditor() && !GIsTransacting);	
				if (IsInLevelEditor())
				{
					ControlProxy->Modify();
				}
				// users may select gizmo and control rig units, so we have to let them go through both of them if they do
				// first go through gizmo actor
				AControlRigGizmoActor* GizmoActor = GetGizmoFromControlName(InKey.Name);
				if (GizmoActor)
				{
					GizmoActor->SetSelected(bSelected);

				}
				if (IsInLevelEditor())
				{
					if (bSelected)
					{
						if (UControlRig* ControlRig = GetControlRig(true))
						{
							FRigControl* Control = ControlRig->FindControl(InKey.Name);
							if (Control)
							{
								ControlProxy->SelectProxy(InKey.Name, true);
							}
						}
					}
					else
					{
						ControlProxy->SelectProxy(InKey.Name, false);
					}
				}
			}
			bSelectionChanged = true;
			
			break;
		}
		default:
		{
			ensureMsgf(false, TEXT("Unsupported Type of RigElement: %d"), InKey.Type);
			break;
		}
	}
}

void FControlRigEditMode::OnRigElementChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	if(!InKey) // all of them changed
	{
		RequestToRecreateGizmoActors();
	}
}

void FControlRigEditMode::OnControlModified(UControlRig* Subject, const FRigControl& Control, const FRigControlModifiedContext& Context)
{
	//this makes sure the details panel ui get's updated, don't remove
	ControlProxy->ProxyChanged(Control.Name);

	/*
	FScopedTransaction ScopedTransaction(LOCTEXT("ModifyControlTransaction", "Modify Control"),!GIsTransacting && Context.SetKey != EControlRigSetKey::Never);
	ControlProxy->Modify();
	RecalcPivotTransform();

	if (UControlRig* ControlRig = static_cast<UControlRig*>(Subject))
	{
		FTransform ComponentTransform = GetHostingSceneComponentTransform();
		if (AControlRigGizmoActor* const* Actor = GizmoToControlMap.FindKey(InControl.Index))
		{
			TickGizmo(*Actor, ComponentTransform);
		}
	}
	*/
}

void FControlRigEditMode::OnControlUISettingChanged(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	// todo: simplify - this is pretty slow for what it does
	RequestToRecreateGizmoActors();
}

void FControlRigEditMode::OnWidgetModeChanged(FWidget::EWidgetMode InWidgetMode)
{
	if (Settings && Settings->bCoordSystemPerWidgetMode)
	{
		TGuardValue<bool> ReentrantGuardSelf(bIsChangingCoordSystem, true);

		int32 WidgetMode = (int32)GLevelEditorModeTools().GetWidgetMode();
		GLevelEditorModeTools().SetCoordSystem(CoordSystemPerWidgetMode[WidgetMode]);
	}
}

void FControlRigEditMode::OnCoordSystemChanged(ECoordSystem InCoordSystem)
{
	TGuardValue<bool> ReentrantGuardSelf(bIsChangingCoordSystem, true);

	int32 WidgetMode = (int32)GLevelEditorModeTools().GetWidgetMode();
	ECoordSystem CoordSystem = GLevelEditorModeTools().GetCoordSystem();
	CoordSystemPerWidgetMode[WidgetMode] = CoordSystem;
}

void FControlRigEditMode::SetGizmoTransform(AControlRigGizmoActor* GizmoActor, const FTransform& InTransform)
{
	if (UControlRig* ControlRig = GetControlRig(true, GizmoActor->ControlRigIndex))
	{
		ControlRig->SetControlGlobalTransform(GizmoActor->ControlName, InTransform);
	}
}

FTransform FControlRigEditMode::GetGizmoTransform(AControlRigGizmoActor* GizmoActor) const
{
	if (UControlRig* ControlRig = GetControlRig(true, GizmoActor->ControlRigIndex))
	{
		return ControlRig->GetControlGlobalTransform(GizmoActor->ControlName);
	}
	return FTransform::Identity;
}

void FControlRigEditMode::MoveGizmo(AControlRigGizmoActor* GizmoActor, const bool bTranslation, FVector& InDrag,
	const bool bRotation, FRotator& InRot, const bool bScale, FVector& InScale, const FTransform& ToWorldTransform,
	bool bUseLocal, bool bCalcLocal, FTransform& InOutLocal)
{
	bool bTransformChanged = false;

	//first case is where we do all controls by the local diff.
	if (bUseLocal)
	{
		if (UControlRig* RuntimeControlRig = GetControlRig(false, GizmoActor->ControlRigIndex))
		{
			UControlRig* InteractionControlRig = GetControlRig(true, GizmoActor->ControlRigIndex);

			FRigControlModifiedContext Context;
			Context.EventName = FRigUnit_BeginExecution::EventName;
			FTransform CurrentLocalTransform = InteractionControlRig->GetControlLocalTransform(GizmoActor->ControlName);
			if (bRotation)
			{

				FQuat CurrentRotation = CurrentLocalTransform.GetRotation();
				CurrentRotation = (CurrentRotation * InOutLocal.GetRotation());
				CurrentLocalTransform.SetRotation(CurrentRotation);
				bTransformChanged = true;
			}

			if (bTranslation)
			{
				FVector CurrentLocation = CurrentLocalTransform.GetLocation();
				CurrentLocation = CurrentLocation + InOutLocal.GetLocation();
				CurrentLocalTransform.SetLocation(CurrentLocation);
				bTransformChanged = true;
			}

			if (bTransformChanged)
			{
				InteractionControlRig->SetControlLocalTransform(GizmoActor->ControlName, CurrentLocalTransform);

				FTransform CurrentTransform  = InteractionControlRig->GetGlobalTransform(GizmoActor->ControlName);			// assumes it's attached to actor
				CurrentTransform = ToWorldTransform * CurrentTransform;

				GizmoActor->SetGlobalTransform(CurrentTransform);

				if (RuntimeControlRig->GetInteractionRig() == InteractionControlRig)
				{
					InteractionControlRig->Evaluate_AnyThread();
				}
			}
		}
	}
	if(!bTransformChanged) //not local or doing scale.
	{
		FTransform CurrentTransform = GetGizmoTransform(GizmoActor)* ToWorldTransform;

		if (bRotation)
		{
			FQuat CurrentRotation = CurrentTransform.GetRotation();
			CurrentRotation = (InRot.Quaternion() * CurrentRotation);
			CurrentTransform.SetRotation(CurrentRotation);
			bTransformChanged = true;
		}

		if (bTranslation)
		{
			FVector CurrentLocation = CurrentTransform.GetLocation();
			CurrentLocation = CurrentLocation + InDrag;
			CurrentTransform.SetLocation(CurrentLocation);
			bTransformChanged = true;
		}

		if (bScale)
		{
			FVector CurrentScale = CurrentTransform.GetScale3D();
			CurrentScale = CurrentScale + InScale;
			CurrentTransform.SetScale3D(CurrentScale);
			bTransformChanged = true;
		}

		if (bTransformChanged)
		{
			if (UControlRig* RuntimeControlRig = GetControlRig(false, GizmoActor->ControlRigIndex))
			{
				UControlRig* InteractionControlRig = GetControlRig(true, GizmoActor->ControlRigIndex);

				FTransform NewTransform = CurrentTransform.GetRelativeTransform(ToWorldTransform);
				FRigControlModifiedContext Context;
				Context.EventName = FRigUnit_BeginExecution::EventName;
				if (bCalcLocal)
				{
					InOutLocal = InteractionControlRig->GetControlLocalTransform(GizmoActor->ControlName);
				}
				InteractionControlRig->SetControlGlobalTransform(GizmoActor->ControlName, NewTransform, Context);			// assumes it's attached to actor
				GizmoActor->SetGlobalTransform(CurrentTransform);
				if (bCalcLocal)
				{
					FTransform NewLocal = InteractionControlRig->GetControlLocalTransform(GizmoActor->ControlName);
					InOutLocal = NewLocal.GetRelativeTransform(InOutLocal);

				}

				if (RuntimeControlRig->GetInteractionRig() == InteractionControlRig)
				{
					InteractionControlRig->Evaluate_AnyThread();
				}
			}
		}
	}
#if WITH_EDITOR
	if (bTransformChanged)
	{
		if (UControlRig* RuntimeControlRig = GetControlRig(false, GizmoActor->ControlRigIndex))
		{
			if (UWorld* World = RuntimeControlRig->GetWorld())
			{
				if (World->IsPreviewWorld())
				{
					if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(RuntimeControlRig->GetClass()->ClassGeneratedBy))
					{
						Blueprint->PropagatePoseFromInstanceToBP(RuntimeControlRig);
					}
				}
			}
		}
	}
#endif
}

// temporarily we just support following types of gizmo
bool IsSupportedControlType(const ERigControlType ControlType)
{
	switch (ControlType)
	{
		case ERigControlType::Float:
		case ERigControlType::Integer:
		case ERigControlType::Vector2D:
		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::EulerTransform:
		{
			return true;
		}
		default:
		{
			break;
		}
	}

	return false;
}

bool FControlRigEditMode::ModeSupportedByGizmoActor(const AControlRigGizmoActor* GizmoActor, FWidget::EWidgetMode InMode) const
{
	if (UControlRig* ControlRig = GetControlRig(true, GizmoActor->ControlRigIndex))
	{
		const FRigControl* RigControl = ControlRig->FindControl(GizmoActor->ControlName);
		if (RigControl)
		{
			if (IsSupportedControlType(RigControl->ControlType))
			{
				switch (InMode)
				{
					case FWidget::WM_Rotate:
					{
						switch (RigControl->ControlType)
						{
							case ERigControlType::Rotator:
							case ERigControlType::Transform:
							case ERigControlType::TransformNoScale:
							case ERigControlType::EulerTransform:
							{
								return true;
							}
							default:
							{
								break;
							}
						}
						break;
					}
					case FWidget::WM_Translate:
					{
						switch (RigControl->ControlType)
						{
							case ERigControlType::Float:
							case ERigControlType::Integer:
							case ERigControlType::Vector2D:
							case ERigControlType::Position:
							case ERigControlType::Transform:
							case ERigControlType::TransformNoScale:
							case ERigControlType::EulerTransform:
							{
								return true;
							}
							default:
							{
								break;
							}
						}
						break;
					}
					case FWidget::WM_Scale:
					{
						switch (RigControl->ControlType)
						{
							case ERigControlType::Scale:
							case ERigControlType::Transform:
							case ERigControlType::EulerTransform:
							{
								return true;
							}
							default:
							{
								break;
							}
						}
						break;
					}
					case FWidget::WM_TranslateRotateZ:
					{
						switch (RigControl->ControlType)
						{
							case ERigControlType::Transform:
							case ERigControlType::TransformNoScale:
							case ERigControlType::EulerTransform:
							{
								return true;
							}
							default:
							{
								break;
							}
						}
						break;
					}
				}
			}
		}
	}
	return false;
}

void FControlRigEditMode::TickGizmo(AControlRigGizmoActor* GizmoActor, const FTransform& ComponentTransform)
{
	if (GizmoActor)
	{
		if (UControlRig* ControlRig = GetControlRig(true, GizmoActor->ControlRigIndex))
		{
			FTransform Transform = ControlRig->GetControlGlobalTransform(GizmoActor->ControlName);
			GizmoActor->SetActorTransform(Transform * ComponentTransform);

			if (FRigControl* Control = ControlRig->FindControl(GizmoActor->ControlName))
			{
				GizmoActor->SetGizmoColor(Control->GizmoColor);
				GizmoActor->SetIsTemporarilyHiddenInEditor(!Control->bGizmoVisible || Settings->bHideManipulators);
				GizmoActor->SetSelectable(Control->bGizmoVisible && !Settings->bHideManipulators && Control->bAnimatable);
			}
		}
	}
}

AControlRigGizmoActor* FControlRigEditMode::GetGizmoFromControlName(const FName& ControlName) const
{
	for (AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		if (GizmoActor->ControlName == ControlName)
		{
			return GizmoActor;
		}
	}

	return nullptr;
}

void FControlRigEditMode::AddControlRig(UControlRig* InControlRig)
{
	RuntimeControlRigs.AddUnique(InControlRig);
}

UControlRig* FControlRigEditMode::GetControlRig(bool bInteractionRig, int32 InIndex) const
{
	if (!RuntimeControlRigs.IsValidIndex(InIndex))
	{
		return nullptr;
	}

	UControlRig* RuntimeControlRig = RuntimeControlRigs[InIndex].Get();
	if (bInteractionRig && RuntimeControlRig)
	{
		if (UControlRig* InteractionControlRig = RuntimeControlRig->GetInteractionRig())
		{
			return InteractionControlRig;
		}
	}
	return RuntimeControlRig;
}

void FControlRigEditMode::RemoveControlRig(UControlRig* InControlRig)
{
	int32 Index = RuntimeControlRigs.Find(InControlRig);
	if (RuntimeControlRigs.IsValidIndex(Index))
	{
		RuntimeControlRigs[Index]->ControlModified().RemoveAll(this);
		RuntimeControlRigs.RemoveAt(Index);

		DelegateHelper->RemoveDelegates();
	}
}

void FControlRigEditMode::TickManipulatableObjects(float DeltaTime)
{
	// tick skeletalmeshcomponent, that's how they update their transform from rig change
	USceneComponent* SceneComponent = GetHostingSceneComponent();
	if (UControlRigComponent* ControlRigComponent = Cast<UControlRigComponent>(SceneComponent))
	{
		ControlRigComponent->Update();
	}
	else if (USkeletalMeshComponent* MeshComponent = Cast<USkeletalMeshComponent>(SceneComponent))
	{
		MeshComponent->RefreshBoneTransforms();
		MeshComponent->RefreshSlaveComponents();
		MeshComponent->UpdateComponentToWorld();
		MeshComponent->FinalizeBoneTransform();
		MeshComponent->MarkRenderTransformDirty();
		MeshComponent->MarkRenderDynamicDataDirty();
	}

	PostPoseUpdate();
}

bool FControlRigEditMode::CreateGizmoActors(UWorld* World)
{
	DestroyGizmosActors();

	FActorSpawnParameters ActorSpawnParameters;
	ActorSpawnParameters.bTemporaryEditorActor = true;

	// for now we only support FTransform
	for (int32 ControlRigIndex = 0; ControlRigIndex < RuntimeControlRigs.Num(); ControlRigIndex++)
	{
		UControlRig* ControlRig = GetControlRig(true, ControlRigIndex);
		if (ControlRig == nullptr)
		{
			continue;
		}

		const TArray<FRigControl>& Controls = ControlRig->AvailableControls();
		UControlRigGizmoLibrary* GizmoLibrary = ControlRig->GetGizmoLibrary();

		for (const FRigControl& Control : Controls)
		{
			if (!Control.bGizmoEnabled)
			{
				continue;
			}
			if (IsSupportedControlType(Control.ControlType))
			{
				FGizmoActorCreationParam Param;
				Param.ManipObj = ControlRig;
				Param.ControlRigIndex = ControlRigIndex;
				Param.ControlName = Control.Name;
				Param.SpawnTransform = ControlRig->GetControlGlobalTransform(Control.Name);
				Param.GizmoTransform = Control.GizmoTransform;
				Param.bSelectable = Control.bAnimatable;

				if (GizmoLibrary)
				{
					if (const FControlRigGizmoDefinition* Gizmo = GizmoLibrary->GetGizmoByName(Control.GizmoName, true /* use default */))
					{
						Param.MeshTransform = Gizmo->Transform;
						Param.StaticMesh = Gizmo->StaticMesh;
						Param.Material = GizmoLibrary->DefaultMaterial;
						Param.ColorParameterName = GizmoLibrary->MaterialColorParameter;
					}
				}

				Param.Color = Control.GizmoColor;

				AControlRigGizmoActor* GizmoActor = FControlRigGizmoHelper::CreateDefaultGizmoActor(World, Param);
				if (GizmoActor)
				{
					GizmoActors.Add(GizmoActor);
				}
			}
		}
	}

	WorldPtr = World;
	OnWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddSP(this, &FControlRigEditMode::OnWorldCleanup);
	return (GizmoActors.Num() > 0);
}

void FControlRigEditMode::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	// if world gets cleaned up first, we destroy gizmo actors
	if (WorldPtr == World)
	{
		DestroyGizmosActors();
	}
}

void FControlRigEditMode::DestroyGizmosActors()
{
	for (AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		UWorld* World = GizmoActor->GetWorld();
		if (World)
		{
			World->DestroyActor(GizmoActor);
		}
	}
	GizmoActors.Reset();

	if (OnWorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupHandle);
	}
}

TSharedPtr<IControlRigObjectBinding> FControlRigEditMode::GetObjectBinding() const
{
	for (TWeakObjectPtr<UControlRig> ControlRig : RuntimeControlRigs)
	{
		if (ControlRig.IsValid())
		{
			return ControlRig->GetObjectBinding();
		}
	}

	return TSharedPtr<IControlRigObjectBinding>();
}

void FControlRigEditMode::SetObjectBinding(TSharedPtr<IControlRigObjectBinding> InObjectBinding)
{
	for (TWeakObjectPtr<UControlRig> ControlRig : RuntimeControlRigs)
	{
		if (ControlRig.IsValid())
		{
			ControlRig->SetObjectBinding(InObjectBinding);
		}
	}
}

USceneComponent* FControlRigEditMode::GetHostingSceneComponent() const
{
	TSharedPtr<IControlRigObjectBinding> ObjectBinding = GetObjectBinding();
	if (ObjectBinding.IsValid())
	{
		return Cast<USceneComponent>(ObjectBinding->GetBoundObject());
	}

	return nullptr;
}

FTransform FControlRigEditMode::GetHostingSceneComponentTransform() const
{
	USceneComponent* HostingComponent = GetHostingSceneComponent();
	return HostingComponent ? HostingComponent->GetComponentTransform() : FTransform::Identity;
}

void FControlRigEditMode::OnPoseInitialized()
{
	OnAnimSystemInitializedDelegate.Broadcast();
}

void FControlRigEditMode::PostPoseUpdate()
{
	FTransform ComponentTransform = GetHostingSceneComponentTransform();
	for(AControlRigGizmoActor* GizmoActor : GizmoActors)
	{
		TickGizmo(GizmoActor, ComponentTransform);
	}

}

#undef LOCTEXT_NAMESPACE
