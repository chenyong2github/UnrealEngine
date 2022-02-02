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
#include "RigVMModel/RigVMController.h"
#include "Rigs/AdditiveControlRig.h"
#include "Rigs/FKControlRig.h"
#include "ControlRigComponent.h"
#include "EngineUtils.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"

//#include "IPersonaPreviewScene.h"
//#include "Animation/DebugSkelMeshComponent.h"
//#include "Persona/Private/AnimationEditorViewportClient.h"
#include "IPersonaPreviewScene.h"
#include "PersonaSelectionProxies.h"
#include "Framework/Application/SlateApplication.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Settings/ControlRigSettings.h"
#include "ToolMenus.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "SRigSpacePickerWidget.h"
#include "ControlRigSpaceChannelEditors.h"

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
	: bIsChangingControlShapeTransform(false)
	, InteractionScope(nullptr)
	, bManipulatorMadeChange(false)
	, bSelecting(false)
	, bSelectionChanged(false)
	, PivotTransform(FTransform::Identity)
	, bRecreateControlShapesRequired(false)
	, bSuspendHierarchyNotifs(false)
	, CurrentViewportClient(nullptr)
	, bIsChangingCoordSystem(false)
{
	ControlProxy = NewObject<UControlRigDetailPanelControlProxies>(GetTransientPackage(), NAME_None);
	ControlProxy->SetFlags(RF_Transactional);

	CommandBindings = MakeShareable(new FUICommandList);
	BindCommands();

#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &FControlRigEditMode::OnObjectsReplaced);
#endif
}

FControlRigEditMode::~FControlRigEditMode()
{	
	CommandBindings = nullptr;

	DestroyShapesActors();
	OnControlRigAddedOrRemovedDelegate.Clear();

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
	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
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

bool FControlRigEditMode::IsInLevelEditor() const
{
	return GetModeManager() == &GLevelEditorModeTools();
}
void FControlRigEditMode::SetUpDetailPanel()
{
	if (IsInLevelEditor() && Toolkit)
	{
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetSequencer(WeakSequencer.Pin());
#if USE_LOCAL_DETAILS
		TArray<TWeakObjectPtr<>> Eulers;
		TArray<TWeakObjectPtr<>> Transforms;
		TArray<TWeakObjectPtr<>> TransformNoScales;
		TArray<TWeakObjectPtr<>> Floats;
		TArray<TWeakObjectPtr<>> Vectors;
		TArray<TWeakObjectPtr<>> Vector2Ds;
		TArray<TWeakObjectPtr<>> Bools;
		TArray<TWeakObjectPtr<>> Integers;
		TArray<TWeakObjectPtr<>> Enums;
		if (UControlRig* ControlRig = GetControlRig(true))
		{
			const TArray<UControlRigControlsProxy*>& Proxies = ControlProxy->GetSelectedProxies();
			for (UControlRigControlsProxy* Proxy : Proxies)
			{
				if (Proxy->GetClass() == UControlRigTransformControlProxy::StaticClass())
				{
					Transforms.Add(Proxy);
				}
				else if (Proxy->GetClass() == UControlRigTransformNoScaleControlProxy::StaticClass())
				{
					TransformNoScales.Add(Proxy);
				}
				else if (Proxy->GetClass() == UControlRigEulerTransformControlProxy::StaticClass())
				{
					Eulers.Add(Proxy);
				}
				else if (Proxy->GetClass() == UControlRigFloatControlProxy::StaticClass())
				{
					Floats.Add(Proxy);
				}
				else if (Proxy->GetClass() == UControlRigVectorControlProxy::StaticClass())
				{
					Vectors.Add(Proxy);
				}
				else if (Proxy->GetClass() == UControlRigVector2DControlProxy::StaticClass())
				{
					Vector2Ds.Add(Proxy);
				}
				else if (Proxy->GetClass() == UControlRigBoolControlProxy::StaticClass())
				{
					Bools.Add(Proxy);
				}
				else if (Proxy->GetClass() == UControlRigEnumControlProxy::StaticClass())
				{
					Enums.Add(Proxy);
				}
				else if (Proxy->GetClass() == UControlRigIntegerControlProxy::StaticClass())
				{
					Integers.Add(Proxy);
				}
			}
		}
		for (TWeakObjectPtr<>& Object : Transforms)
		{
			UControlRigControlsProxy* Proxy = Cast<UControlRigControlsProxy>(Object.Get());
			if (Proxy)
			{
				Proxy->SetIsMultiple(Transforms.Num() > 1);
			}
		}
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetTransformDetailsObjects(Transforms);
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetTransformNoScaleDetailsObjects(TransformNoScales);
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetEulerTransformDetailsObjects(Eulers);
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetFloatDetailsObjects(Floats);
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetVectorDetailsObjects(Vectors);
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetVector2DDetailsObjects(Vector2Ds);
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetBoolDetailsObjects(Bools);
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetIntegerDetailsObjects(Integers);
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetEnumDetailsObjects(Enums);
#else 
		StaticCastSharedPtr<SControlRigEditModeTools>(Toolkit->GetInlineContent())->SetSettingsDetailsObject(GetMutableDefault<UControlRigEditModeSettings>());	
#endif

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
		InteractionControlRig->GetHierarchy()->OnModified().RemoveAll(this);
		InteractionControlRig->ControlModified().RemoveAll(this);
			
		InteractionControlRig->GetHierarchy()->OnModified().AddSP(this, &FControlRigEditMode::OnHierarchyModified);
		InteractionControlRig->ControlModified().AddSP(this, &FControlRigEditMode::OnControlModified);
	}

	if (!RuntimeControlRig)
	{
		DestroyShapesActors();
		SetUpDetailPanel();
	}
	else
	{
		// create default manipulation layer
		RequestToRecreateControlShapeActors();
	}
}

bool FControlRigEditMode::UsesToolkits() const
{
	return true;
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

		FEditorModeTools* ModeManager = GetModeManager();

		bIsChangingCoordSystem = false;
		if (CoordSystemPerWidgetMode.Num() < (UE::Widget::WM_Max))
		{
			CoordSystemPerWidgetMode.SetNum(UE::Widget::WM_Max);
			ECoordSystem CoordSystem = ModeManager->GetCoordSystem();
			for (int32 i = 0; i < UE::Widget::WM_Max; ++i)
			{
				CoordSystemPerWidgetMode[i] = CoordSystem;
			}
		}
	
		ModeManager->OnWidgetModeChanged().AddSP(this, &FControlRigEditMode::OnWidgetModeChanged);
		ModeManager->OnCoordSystemChanged().AddSP(this, &FControlRigEditMode::OnCoordSystemChanged);
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
		Toolkit.Reset();
	}

	DestroyShapesActors();
	OnControlRigAddedOrRemovedDelegate.Clear();

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
	FEditorModeTools* ModeManager = GetModeManager();
	ModeManager->OnWidgetModeChanged().RemoveAll(this);
	ModeManager->OnCoordSystemChanged().RemoveAll(this);

	//clear proxies
	ControlProxy->RemoveAllProxies();

	//make sure the widget is reset
	ResetControlShapeSize();

	// Call parent implementation
	FEdMode::Exit();
}

void FControlRigEditMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if(DeferredItemsToFrame.Num() > 0)
	{
		TGuardValue<FEditorViewportClient*> ViewportGuard(CurrentViewportClient, ViewportClient);
		FrameItems(DeferredItemsToFrame);
		DeferredItemsToFrame.Reset();
	}

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

	// Defer creation of shapes if manipulating the viewport
	if (bRecreateControlShapesRequired && !(FSlateApplication::Get().HasAnyMouseCaptor() || GUnrealEd->IsUserInteracting()))
	{
		RecreateControlShapeActors();
		TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
		for (const FRigElementKey& SelectedKey : SelectedRigElements)
		{
			if (SelectedKey.Type == ERigElementType::Control)
			{
				AControlRigShapeActor* ShapeActor = GetControlShapeFromControlName(SelectedKey.Name);
				if (ShapeActor)
				{
					ShapeActor->SetSelected(true);
				}

				if (IsInLevelEditor())
				{
					if (UControlRig* ControlRig = GetControlRig(true))
					{
						FRigControlElement* ControlElement = ControlRig->FindControl(SelectedKey.Name);
						if (ControlElement)
						{
							if (!ControlRig->IsCurveControl(ControlElement))
							{
								ControlProxy->AddProxy(SelectedKey.Name, ControlRig, ControlElement);
							}
						}
					}
				}
			}
		}
		SetUpDetailPanel();
		HandleSelectionChanged();
		bRecreateControlShapesRequired = false;
	}

	// We need to tick here since changing a bone for example
	// might have changed the transform of the Control
	{
		PostPoseUpdate();

		if (UControlRig* ControlRig = GetControlRig(true))
		{
			TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
			const UE::Widget::EWidgetMode CurrentWidgetMode = ViewportClient->GetWidgetMode();
			for (FRigElementKey SelectedRigElement : SelectedRigElements)
			{
				//need to loop through the shape actors and set widget based upon the first one
				if (AControlRigShapeActor* ShapeActor = GetControlShapeFromControlName(SelectedRigElement.Name))
				{
					if (!ModeSupportedByShapeActor(ShapeActor, CurrentWidgetMode))
					{
						if (FRigControlElement* ControlElement = ControlRig->FindControl(SelectedRigElement.Name))
						{
							switch (ControlElement->Settings.ControlType)
							{
								case ERigControlType::Float:
								case ERigControlType::Integer:
								case ERigControlType::Vector2D:
								case ERigControlType::Position:
								case ERigControlType::Transform:
								case ERigControlType::TransformNoScale:
								case ERigControlType::EulerTransform:
								{
									ViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
									break;
								}
								case ERigControlType::Rotator:
								{
									ViewportClient->SetWidgetMode(UE::Widget::WM_Rotate);
									break;
								}
								case ERigControlType::Scale:
								{
									ViewportClient->SetWidgetMode(UE::Widget::WM_Scale);
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
//Hit proxy for FK Rigs and bones.
struct  HFKRigBoneProxy : public HHitProxy
{
	DECLARE_HIT_PROXY()

	FName BoneName;
	UControlRig* ControlRig;

	HFKRigBoneProxy()
		: HHitProxy(HPP_Foreground)
		, BoneName(NAME_None)
		, ControlRig(nullptr)
	{}

	HFKRigBoneProxy(FName InBoneName, UControlRig *InControlRig)
		: HHitProxy(HPP_Foreground)
		, BoneName(InBoneName)
		, ControlRig(InControlRig)
	{
	}

	// HHitProxy interface
	virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Crosshairs; }
	// End of HHitProxy interface
};

IMPLEMENT_HIT_PROXY(HFKRigBoneProxy, HHitProxy)


TSet<FName> FControlRigEditMode::GetActiveControlsFromSequencer(UControlRig* ControlRig)
{
	TSet<FName> ActiveControls;
	if (WeakSequencer.IsValid() == false)
	{
		return ActiveControls;
	}
	if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
	{
		USceneComponent* Component = Cast<USceneComponent>(ObjectBinding->GetBoundObject());
		if (!Component)
		{
			return ActiveControls;
		}
		const bool bCreateHandleIfMissing = false;
		FName CreatedFolderName = NAME_None;
		FGuid ObjectHandle = WeakSequencer.Pin()->GetHandleToObject(Component, bCreateHandleIfMissing);
		if (!ObjectHandle.IsValid())
		{
			UObject* ActorObject = Component->GetOwner();
			ObjectHandle = WeakSequencer.Pin()->GetHandleToObject(ActorObject, bCreateHandleIfMissing);
			if (!ObjectHandle.IsValid())
			{
				return ActiveControls;
			}
		}
		bool bCreateTrack = false;
		UMovieScene* MovieScene = WeakSequencer.Pin()->GetFocusedMovieSceneSequence()->GetMovieScene();
		if (!MovieScene)
		{
			return ActiveControls;
		}
		if (FMovieSceneBinding* Binding = MovieScene->FindBinding(ObjectHandle))
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				if (UMovieSceneControlRigParameterTrack* ControlRigParameterTrack = Cast<UMovieSceneControlRigParameterTrack>(Track))
				{
					if (ControlRigParameterTrack->GetControlRig() == ControlRig)
					{
						UMovieSceneControlRigParameterSection* ActiveSection = Cast<UMovieSceneControlRigParameterSection>(ControlRigParameterTrack->GetSectionToKey());
						if (ActiveSection)
						{
							TArray<FRigControlElement*> Controls;
							ControlRig->GetControlsInOrder(Controls);
							TArray<bool> Mask = ActiveSection->GetControlsMask();

							TArray<FName> Names;
							int Index = 0;
							for (FRigControlElement* ControlElement : Controls)
							{
								if (Mask[Index])
								{
									ActiveControls.Add(ControlElement->GetName());
								}
								++Index;
							}
						}
					}
				}
			}
		}
	}
	return ActiveControls;
}


void FControlRigEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{	
	UControlRig* ControlRig = GetControlRig(false);
	if(ControlRig == nullptr)
	{
		return;
	}

	const UControlRigEditModeSettings* Settings = GetDefault<UControlRigEditModeSettings>();

	bool bRender = !Settings->bHideControlShapes;

	FTransform ComponentTransform = FTransform::Identity;
	if(IsInLevelEditor())
	{
		ComponentTransform = GetHostingSceneComponentTransform();
	}
	
	if (bRender)
	{
		for (AControlRigShapeActor* Actor : ShapeActors)
		{
			//Actor->SetActorHiddenInGame(bIsHidden);
			if (GIsEditor && Actor->GetWorld() != nullptr && !Actor->GetWorld()->IsPlayInEditor())
			{
				Actor->SetIsTemporarilyHiddenInEditor(false);
			}
		}

		URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
		const bool bHasFKRig = (ControlRig->IsA<UAdditiveControlRig>() || ControlRig->IsA<UFKControlRig>());
		if (Settings->bDisplayHierarchy || bHasFKRig)
		{
			const bool bBoolSetHitProxies = PDI && PDI->IsHitTesting() && bHasFKRig;
			TSet<FName> ActiveControlName;
			if (bHasFKRig)
			{
				ActiveControlName = GetActiveControlsFromSequencer(ControlRig);
			}
			Hierarchy->ForEach<FRigTransformElement>([PDI, Hierarchy, ComponentTransform,ControlRig, bHasFKRig, bBoolSetHitProxies, ActiveControlName](FRigTransformElement* TransformElement) -> bool
			{
				const FTransform Transform = Hierarchy->GetTransform(TransformElement, ERigTransformType::CurrentGlobal);

				FRigBaseElementParentArray Parents = Hierarchy->GetParents(TransformElement);
				for(FRigBaseElement* ParentElement : Parents)
				{
					if(FRigTransformElement* ParentTransformElement = Cast<FRigTransformElement>(ParentElement))
					{
						FLinearColor Color = FLinearColor::White;
						if (bHasFKRig)
						{
							FName ControlName = UFKControlRig::GetControlName(ParentTransformElement->GetName());
							if (ActiveControlName.Num() > 0 && ActiveControlName.Contains(ControlName) == false)
							{
								continue;
							}
							if (ControlRig->IsControlSelected(ControlName))
							{
								Color = FLinearColor::Yellow;
							}
						}
						const FTransform ParentTransform = Hierarchy->GetTransform(ParentTransformElement, ERigTransformType::CurrentGlobal);
						const bool bHitTesting = bBoolSetHitProxies && (ParentTransformElement->GetType() == ERigElementType::Bone);
						if (bHitTesting)
						{
							PDI->SetHitProxy(new HFKRigBoneProxy(ParentTransformElement->GetName(), ControlRig));
						}
                        PDI->DrawLine(ComponentTransform.TransformPosition(Transform.GetLocation()),ComponentTransform.TransformPosition(ParentTransform.GetLocation()), Color, SDPG_Foreground);
						if (bHitTesting)
						{
							PDI->SetHitProxy(nullptr);
						}
					}
                }

				FLinearColor Color = FLinearColor::White;
				if (bHasFKRig)
				{
					FName ControlName = UFKControlRig::GetControlName(TransformElement->GetName());
					if (ActiveControlName.Num() > 0 && ActiveControlName.Contains(ControlName) == false)
					{
						return true;
					}
					if (ControlRig->IsControlSelected(ControlName))
					{
						Color = FLinearColor::Yellow;
					}
				}
				const bool bHitTesting = PDI && PDI->IsHitTesting() && bBoolSetHitProxies && (TransformElement->GetType() == ERigElementType::Bone);
				if (bHitTesting)
				{
					PDI->SetHitProxy(new HFKRigBoneProxy(TransformElement->GetName(), ControlRig));
				}
				PDI->DrawPoint(ComponentTransform.TransformPosition(Transform.GetLocation()), Color, 5.0f, SDPG_Foreground);

				if (bHitTesting)
				{
					PDI->SetHitProxy(nullptr);
				}

				return true;
			});
		}

		if (Settings->bDisplayNulls || ControlRig->IsSetupModeEnabled())
		{
			TArray<FTransform> SpaceTransforms;
			TArray<FTransform> SelectedSpaceTransforms;
			Hierarchy->ForEach<FRigNullElement>([&SpaceTransforms, &SelectedSpaceTransforms, Hierarchy](FRigNullElement* NullElement) -> bool
            {
				if(Hierarchy->IsSelected(NullElement->GetIndex()))
				{
					SelectedSpaceTransforms.Add(Hierarchy->GetTransform(NullElement, ERigTransformType::CurrentGlobal));
				}
				else
				{
					SpaceTransforms.Add(Hierarchy->GetTransform(NullElement, ERigTransformType::CurrentGlobal));
				}
				return true;
			});

			GetControlRig(true)->DrawInterface.DrawAxes(FTransform::Identity, SpaceTransforms, Settings->AxisScale);
			GetControlRig(true)->DrawInterface.DrawAxes(FTransform::Identity, SelectedSpaceTransforms, FLinearColor(1.0f, 0.34f, 0.0f, 1.0f), Settings->AxisScale);
		}

		if (Settings->bDisplayAxesOnSelection && Settings->AxisScale > SMALL_NUMBER)
		{
			if (ControlRig->GetWorld() && ControlRig->GetWorld()->IsPreviewWorld())
			{
				TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
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
		for (AControlRigShapeActor* Actor : ShapeActors)
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
				TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
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
			else if(UControlRigEditorSettings::Get()->bEnableUndoForPoseInteraction)
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
	for (const AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		if (ShapeActor->IsSelected())
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

bool FControlRigEditMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	for (const AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		if (ShapeActor->IsSelected())
		{
			return ModeSupportedByShapeActor(ShapeActor, CheckMode);
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
			if (ActorHitProxy->Actor->IsA<AControlRigShapeActor>())
			{
				AControlRigShapeActor* ShapeActor = CastChecked<AControlRigShapeActor>(ActorHitProxy->Actor);
				if (ShapeActor->IsSelectable())
				{
					FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), IsInLevelEditor() && !GIsTransacting);

					// temporarily disable the interaction scope
					const bool bInteractionScopePresent = InteractionScope != nullptr; 
					if (bInteractionScopePresent)
					{
						delete InteractionScope;
						InteractionScope = nullptr;
					}
					
					const FName& ControlName = ShapeActor->ControlName;
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
						//also need to clear actor selection. Sequencer will handle this automatically if done in Sequencder UI but not if done by clicking
						if (IsInLevelEditor())
						{
							if (GEditor && GEditor->GetSelectedActorCount())
							{
								const FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "UpdatingActorComponentSelectionNone", "Select None"), !GIsTransacting);
								GEditor->SelectNone(false, true);
								GEditor->NoteSelectionChange();
							}
						}
						ClearRigElementSelection(FRigElementTypeHelper::ToMask(ERigElementType::Control));
						SetRigElementSelection(ERigElementType::Control, ControlName, true);
					}

					if (bInteractionScopePresent)
					{
						if(UControlRig* ControlRig = GetControlRig(true))
						{
							InteractionScope = new FControlRigInteractionScope(ControlRig);
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

									if (Click.IsShiftDown()) //guess we just select
									{
										SetRigElementSelection(ERigElementType::Control, ControlName, true);
									}
									else if (Click.IsControlDown()) //if ctrl we toggle selection
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
									return true;
								}
							}
						}
					}
				}
			}
		}
	}
	else if (HFKRigBoneProxy* FKBoneProxy = HitProxyCast<HFKRigBoneProxy>(HitProxy))
	{
		FName ControlName(*(FKBoneProxy->BoneName.ToString() + TEXT("_CONTROL")));
		if (FKBoneProxy->ControlRig->FindControl(ControlName))
		{
			FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), IsInLevelEditor() && !GIsTransacting);

			if (Click.IsShiftDown()) //guess we just select
			{
				SetRigElementSelection(ERigElementType::Control, ControlName, true);
			}
			else if (Click.IsControlDown()) //if ctrl we toggle selection
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
			return true;
		}
	}
	else if (HPersonaBoneHitProxy* BoneHitProxy = HitProxyCast<HPersonaBoneHitProxy>(HitProxy))
	{
		if (UControlRig* DebuggedControlRig = GetControlRig(false))
		{
			URigHierarchy* Hierarchy = DebuggedControlRig->GetHierarchy();

			// Cache mapping?
			for (int32 Index = 0; Index < Hierarchy->Num(); Index++)
			{
				const FRigElementKey ElementToSelect = Hierarchy->GetKey(Index);
				if (ElementToSelect.Type == ERigElementType::Bone && ElementToSelect.Name == BoneHitProxy->BoneName)
				{
					if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
					{
						Hierarchy->GetController()->SelectElement(ElementToSelect, true);
					}
					else if (FSlateApplication::Get().GetModifierKeys().IsControlDown())
					{
						const bool bSelect = !Hierarchy->IsSelected(ElementToSelect);
						Hierarchy->GetController()->SelectElement(ElementToSelect, bSelect);
					}
					else
					{
						TArray<FRigElementKey> NewSelection;
						NewSelection.Add(ElementToSelect);
						Hierarchy->GetController()->SetSelection(NewSelection);
					}
					return true;
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

	const UControlRigEditModeSettings* Settings = GetDefault<UControlRigEditModeSettings>();

	if (Settings && Settings->bOnlySelectRigControls)
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

	if (OnGetContextMenuDelegate.IsBound())
	{
		TSharedPtr<SWidget> MenuWidget = SNullWidget::NullWidget;
		
		if (UToolMenu* ContextMenu = OnGetContextMenuDelegate.Execute())
		{
			UToolMenus* ToolMenus = UToolMenus::Get();
			MenuWidget = ToolMenus->GenerateWidget(ContextMenu);
		}

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

bool FControlRigEditMode::IntersectSelect(bool InSelect, const TFunctionRef<bool(const AControlRigShapeActor*, const FTransform&)>& Intersects)
{
	FTransform ComponentTransform = GetHostingSceneComponentTransform();

	bool bSelected = false;
	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		if (ShapeActor->IsHiddenEd())
		{
			continue;
		}

		const FTransform ControlTransform = ShapeActor->GetGlobalTransform() * ComponentTransform;
		if (Intersects(ShapeActor, ControlTransform))
		{
			SetRigElementSelection(ERigElementType::Control, ShapeActor->ControlName, InSelect);
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

		if (!Actor->IsA<AControlRigShapeActor>())
		{
			continue;
		}

		AControlRigShapeActor* ShapeActor = CastChecked<AControlRigShapeActor>(Actor);
		if (!ShapeActor->IsSelectable())
		{
			continue;
		}

		if (IntersectsBox(*Actor, InBox, LevelViewportClient, bStrictDragSelection))
		{
			bSomethingSelected = true;
			const FName& ControlName = ShapeActor->ControlName;
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

	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		for (UActorComponent* Component : ShapeActor->GetComponents())
		{
			UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
			if (PrimitiveComponent && PrimitiveComponent->IsRegistered() && PrimitiveComponent->IsVisibleInEditor())
			{
				if (PrimitiveComponent->ComponentIsTouchingSelectionFrustum(InFrustum, InViewportClient->EngineShowFlags, false /*only bsp*/, false/*encompass entire*/))
				{
					if (ShapeActor->IsSelectable())
					{
						bSomethingSelected = true;
						const FName& ControlName = ShapeActor->ControlName;
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

	const UE::Widget::EWidgetMode WidgetMode = InViewportClient->GetWidgetMode();
	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	const ECoordSystem CoordSystem = InViewportClient->GetWidgetCoordSystemSpace();

	const bool bDoRotation = !Rot.IsZero() && (WidgetMode == UE::Widget::WM_Rotate || WidgetMode == UE::Widget::WM_TranslateRotateZ);
	const bool bDoTranslation = !Drag.IsZero() && (WidgetMode == UE::Widget::WM_Translate || WidgetMode == UE::Widget::WM_TranslateRotateZ);
	const bool bDoScale = !Scale.IsZero() && WidgetMode == UE::Widget::WM_Scale;


	if (InteractionScope != nullptr && bMouseButtonDown && !bCtrlDown && !bShiftDown && CurrentAxis != EAxisList::None
		&& (bDoRotation || bDoTranslation || bDoScale))
	{
			if (AreRigElementsSelected(FRigElementTypeHelper::ToMask(ERigElementType::Control)))
		{
			FTransform ComponentTransform = GetHostingSceneComponentTransform();

			if (bIsChangingControlShapeTransform)
			{
				for (AControlRigShapeActor* ShapeActor : ShapeActors)
				{
					if (ShapeActor->IsSelected())
					{
						if (bManipulatorMadeChange == false)
						{
							GEditor->BeginTransaction(LOCTEXT("ChangeControlShapeTransaction", "Change Control Shape Transform"));
						}

						ChangeControlShapeTransform(ShapeActor, bDoTranslation, InDrag, bDoRotation, InRot, bDoScale, InScale, ComponentTransform);
						bManipulatorMadeChange = true;

						// break here since we only support changing shape transform of a single control at a time
						break;
					}
				}
			}
			else
			{
				const UControlRigEditModeSettings* Settings = GetDefault<UControlRigEditModeSettings>();
				bool bDoLocal = (CoordSystem == ECoordSystem::COORD_Local && Settings && Settings->bLocalTransformsInEachLocalSpace);
				bool bUseLocal = false;
				bool bCalcLocal = bDoLocal;
				bool bFirstTime = true;
				FTransform InOutLocal = FTransform::Identity;
				for (AControlRigShapeActor* ShapeActor : ShapeActors)
				{
					if (ShapeActor->IsSelected())
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

						MoveControlShape(ShapeActor, bDoTranslation, InDrag, bDoRotation, InRot, bDoScale, InScale, ComponentTransform,
							bUseLocal, bDoLocal, InOutLocal);
						bManipulatorMadeChange = true;
					}
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
			TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();

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
	return OtherModeID == FName(TEXT("EM_SequencerMode"), FNAME_Find) || OtherModeID == FName(TEXT("MotionTrailEditorMode"), FNAME_Find); /*|| OtherModeID == FName(TEXT("EditMode.ControlRigEditor"), FNAME_Find);*/
}

void FControlRigEditMode::AddReferencedObjects( FReferenceCollector& Collector )
{
	if (ShapeActors.Num() > 0)
	{
		for (AControlRigShapeActor* ShapeActor : ShapeActors)
		{
			Collector.AddReferencedObject(ShapeActor);
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
	if (IsInLevelEditor())
	{
		if(URigHierarchyController* Controller = InteractionRig->GetHierarchy()->GetController())
		{
			Controller->ClearSelection();
		}
	}
	else if (Blueprint)
	{
		Blueprint->GetHierarchyController()->ClearSelection();
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
	if (IsInLevelEditor())
	{
		if(URigHierarchyController* Controller = InteractionRig->GetHierarchy()->GetController())
		{
			Controller->SelectElement(FRigElementKey(InRigElementName, Type), bSelected);
		}
	}
	else if (Blueprint)
	{
		Blueprint->GetHierarchyController()->SelectElement(FRigElementKey(InRigElementName, Type), bSelected);
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

TArray<FRigElementKey> FControlRigEditMode::GetSelectedRigElements() const
{
	TArray<FRigElementKey> SelectedKeys;
	
	if (UControlRig* ControlRig = GetControlRig(true))
	{
		if (ControlRig->GetHierarchy())
		{
			SelectedKeys = ControlRig->GetHierarchy()->GetSelectedKeys();
		}

		// currently only 1 transient control is allowed at a time
		// Transient Control's bSelected flag is never set to true, probably to avoid confusing other parts of the system
		// But since Edit Mode directly deals with transient controls, its selection status is given special treatment here.
		// So basically, whenever a bone is selected, and there is a transient control present, we consider both selected.
		if (SelectedKeys.Num() == 1)
		{
			if (SelectedKeys[0].Type == ERigElementType::Bone || SelectedKeys[0].Type == ERigElementType::Null)
			{
				const FName ControlName = UControlRig::GetNameForTransientControl(SelectedKeys[0]);
				const FRigElementKey TransientControlKey = FRigElementKey(ControlName, ERigElementType::Control);
				if(ControlRig->GetHierarchy()->Contains(TransientControlKey))
				{
					SelectedKeys.Add(TransientControlKey);
				}

			}
		}
		else
		{
			// check if there is a pin value transient control active
			// when a pin control is active, all existing selection should have been cleared
			TArray<FRigControlElement*> TransientControls = ControlRig->GetHierarchy()->GetTransientControls();

			if (TransientControls.Num() > 0)
			{
				if (ensure(SelectedKeys.Num() == 0))
				{
					SelectedKeys.Add(TransientControls[0]->GetKey());
				}
			}
		}
	}

	return SelectedKeys;
}

bool FControlRigEditMode::AreRigElementsSelected(uint32 InTypes) const
{
	TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();

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
	TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
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
	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		TInlineComponentArray<USceneComponent*> SceneComponents;
		ShapeActor->GetComponents(SceneComponents, true);
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
	TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
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
		
		if (bIsChangingControlShapeTransform)
		{
			if (UControlRig* ControlRig = GetControlRig(true))
			{ 
				for (const AControlRigShapeActor* ShapeActor : ShapeActors)
				{
					if (ShapeActor->IsSelected())
					{
						if (FRigControlElement* ControlElement = ControlRig->GetHierarchy()->Find<FRigControlElement>(FRigElementKey(ShapeActor->ControlName, ERigElementType::Control)))
						{
							PivotTransform = ControlRig->GetHierarchy()->GetControlShapeTransform(ControlElement, ERigTransformType::CurrentGlobal);
						}

						// break here since we don't want to change the shape transform of multiple controls.
						break;
					}
				}
			}
		}
		else
		{
			const UControlRigEditModeSettings* Settings = GetDefault<UControlRigEditModeSettings>();
			for (const AControlRigShapeActor* ShapeActor : ShapeActors)
			{
				if (ShapeActor->IsSelected())
				{
					LastTransform = ShapeActor->GetActorTransform().GetRelativeTransform(ComponentTransform);
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

	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		ShapeActor->GetComponents(PrimitiveComponents, true);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			PrimitiveComponent->PushSelectionToProxy();
		}
	}

	// automatically exit shape transform edit mode if there is no shape selected
	if (bIsChangingControlShapeTransform)
	{
		if (!CanChangeControlShapeTransform())
		{
			bIsChangingControlShapeTransform = false;
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
		Commands.IncreaseControlShapeSize,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::IncreaseShapeSize));

	CommandBindings->MapAction(
		Commands.DecreaseControlShapeSize,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::DecreaseShapeSize));

	CommandBindings->MapAction(
		Commands.ResetControlShapeSize,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::ResetControlShapeSize));

	CommandBindings->MapAction(
		Commands.ToggleControlShapeTransformEdit,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::ToggleControlShapeTransformEdit));

	CommandBindings->MapAction(
		Commands.OpenSpacePickerWidget,
		FExecuteAction::CreateRaw(this, &FControlRigEditMode::OpenSpacePickerWidget));
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
		AControlRigShapeActor* ShapeActor = GetControlShapeFromControlName(InElement.Name);
		if (ShapeActor && ensure(ShapeActor->IsSelected()))
		{
			OutGlobalTransform = GetControlShapeTransform(ShapeActor);
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
	return  GetSelectedRigElements().Num() > 0;
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
			return;
		}
    }

	TArray<AActor*> Actors;
	TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
	for (const FRigElementKey& SelectedKey : SelectedRigElements)
	{
		if (SelectedKey.Type == ERigElementType::Control)
		{
			AControlRigShapeActor* ShapeActor = GetControlShapeFromControlName(SelectedKey.Name);
			if (ShapeActor)
			{
				Actors.Add(ShapeActor);
			}
		}
	}

	if (Actors.Num())
	{
		TArray<UPrimitiveComponent*> SelectedComponents;
		GEditor->MoveViewportCamerasToActor(Actors, SelectedComponents, true);
	}
}

void FControlRigEditMode::FrameItems(const TArray<FRigElementKey>& InItems)
{
	if(!OnGetRigElementTransformDelegate.IsBound())
	{
		return;
	}

	if(CurrentViewportClient == nullptr)
	{
		DeferredItemsToFrame = InItems;
		return;
	}

	FBox Box(ForceInit);

	for (int32 Index = 0; Index < InItems.Num(); ++Index)
	{
		static const float Radius = 20.f;
		if (InItems[Index].Type == ERigElementType::Bone || InItems[Index].Type == ERigElementType::Null)
		{
			FTransform Transform = OnGetRigElementTransformDelegate.Execute(InItems[Index], false, true);
			Box += Transform.TransformPosition(FVector::OneVector * Radius);
			Box += Transform.TransformPosition(FVector::OneVector * -Radius);
		}
		else if (InItems[Index].Type == ERigElementType::Control)
		{
			FTransform Transform = OnGetRigElementTransformDelegate.Execute(InItems[Index], false, true);
			Box += Transform.TransformPosition(FVector::OneVector * Radius);
			Box += Transform.TransformPosition(FVector::OneVector * -Radius);
		}
	}

	if(Box.IsValid)
	{
		CurrentViewportClient->FocusViewportOnBox(Box);
	}
}

void FControlRigEditMode::IncreaseShapeSize()
{
	UControlRigEditModeSettings* Settings = GetMutableDefault<UControlRigEditModeSettings>();
	Settings->GizmoScale += 0.1f;
	GetModeManager()->SetWidgetScale(Settings->GizmoScale);
}

void FControlRigEditMode::DecreaseShapeSize()
{
	UControlRigEditModeSettings* Settings = GetMutableDefault<UControlRigEditModeSettings>();
	Settings->GizmoScale -= 0.1f;
	GetModeManager()->SetWidgetScale(Settings->GizmoScale);
}

void FControlRigEditMode::ResetControlShapeSize()
{
	UControlRigEditModeSettings* Settings = GetMutableDefault<UControlRigEditModeSettings>();
	Settings->GizmoScale = 1.0f;
	GetModeManager()->SetWidgetScale(Settings->GizmoScale);
}

void FControlRigEditMode::ToggleControlShapeTransformEdit()
{ 
	if (bIsChangingControlShapeTransform)
	{
		bIsChangingControlShapeTransform = false;
	}
	else if (CanChangeControlShapeTransform())
	{
		bIsChangingControlShapeTransform = true;
	}
}

void FControlRigEditMode::OpenSpacePickerWidget()
{
	UControlRig* RuntimeRig = GetControlRig(false); //helge space picker only works on runtime rig?
	if (RuntimeRig == nullptr)
	{
		return;
	}

	URigHierarchy* Hierarchy = RuntimeRig->GetHierarchy();
	TArray<FRigElementKey> SelectedControls = Hierarchy->GetSelectedKeys(ERigElementType::Control);

	TSharedRef<SRigSpacePickerWidget> PickerWidget =
	SNew(SRigSpacePickerWidget)
	.Hierarchy(Hierarchy)
	.Controls(SelectedControls)
	.Title(LOCTEXT("PickSpace", "Pick Space"))
	.AllowDelete(!IsInLevelEditor())
	.AllowReorder(!IsInLevelEditor())
	.AllowAdd(!IsInLevelEditor())
	.GetControlCustomization_Lambda([this, RuntimeRig](URigHierarchy*, const FRigElementKey& InControlKey)
	{
		return RuntimeRig->GetControlCustomization(InControlKey);
	})
	.OnActiveSpaceChanged_Lambda([this, SelectedControls, RuntimeRig](URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const FRigElementKey& InSpaceKey)
	{
		check(SelectedControls.Contains(InControlKey));
		if (IsInLevelEditor())
		{
			if (WeakSequencer.IsValid())
			{

				if (const FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey))
				{
					ISequencer* Sequencer = WeakSequencer.Pin().Get();
					if (Sequencer)
					{
						FScopedTransaction Transaction(LOCTEXT("KeyControlRigSpace", "Key Control Rig Space"));
						FSpaceChannelAndSection SpaceChannelAndSection = FControlRigSpaceChannelHelpers::FindSpaceChannelAndSectionForControl(RuntimeRig, InControlKey.Name, Sequencer, true /*bCreateIfNeeded*/);
						if (SpaceChannelAndSection.SpaceChannel)
						{
							const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
							const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
							FFrameNumber CurrentTime = FrameTime.GetFrame();
							FControlRigSpaceChannelHelpers::SequencerKeyControlRigSpaceChannel(RuntimeRig, Sequencer, SpaceChannelAndSection.SpaceChannel, SpaceChannelAndSection.SectionToKey, CurrentTime, InHierarchy, InControlKey, InSpaceKey);
						}
					}
				}
			}
		}
		else
		{
			const FTransform Transform = InHierarchy->GetGlobalTransform(InControlKey);
			URigHierarchy::TElementDependencyMap Dependencies = InHierarchy->GetDependenciesForVM(RuntimeRig->GetVM());
			InHierarchy->SwitchToParent(InControlKey, InSpaceKey, false, true, Dependencies, nullptr);
			InHierarchy->SetGlobalTransform(InControlKey, Transform);
		}
		
	})
	.OnSpaceListChanged_Lambda([this, SelectedControls, RuntimeRig](URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const TArray<FRigElementKey>& InSpaceList)
	{
		check(SelectedControls.Contains(InControlKey));

		// check if we are in the control rig editor or in the level
		if(!IsInLevelEditor())
		{
			if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(RuntimeRig->GetClass()->ClassGeneratedBy))
			{
				if(URigHierarchy* Hierarchy = Blueprint->Hierarchy)
				{
					// update the settings in the control element
					if(FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(InControlKey))
					{
						Blueprint->Modify();
						FScopedTransaction Transaction(LOCTEXT("ControlChangeAvailableSpaces", "Edit Available Spaces"));

						ControlElement->Settings.Customization.AvailableSpaces = InSpaceList;
						Hierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
					}

					// also update the debugged instance
					if(Hierarchy != InHierarchy)
					{
						if(FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey))
						{
							ControlElement->Settings.Customization.AvailableSpaces = InSpaceList;
							InHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
						}
					}
				}
			}
		}
		else
		{
			// update the settings in the control element
			if(FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey))
			{
				FScopedTransaction Transaction(LOCTEXT("ControlChangeAvailableSpaces", "Edit Available Spaces"));

				InHierarchy->Modify();

				FRigControlElementCustomization ControlCustomization = *RuntimeRig->GetControlCustomization(InControlKey);	
				ControlCustomization.AvailableSpaces = InSpaceList;
				ControlCustomization.RemovedSpaces.Reset();

				// remember  the elements which are in the asset's available list but removed by the user
				for(const FRigElementKey& AvailableSpace : ControlElement->Settings.Customization.AvailableSpaces)
				{
					if(!ControlCustomization.AvailableSpaces.Contains(AvailableSpace))
					{
						ControlCustomization.RemovedSpaces.Add(AvailableSpace);
					}
				}

				RuntimeRig->SetControlCustomization(InControlKey, ControlCustomization);
				InHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
			}
		}

	});
	// todo: implement GetAdditionalSpacesDelegate to pull spaces from sequencer

	PickerWidget->OpenDialog(false);
}

FText FControlRigEditMode::GetToggleControlShapeTransformEditHotKey() const
{
	const FControlRigEditModeCommands& Commands = FControlRigEditModeCommands::Get();
	return Commands.ToggleControlShapeTransformEdit->GetInputText();
}

void FControlRigEditMode::ToggleManipulators()
{
	// Toggle flag (is used in drawing code)
	UControlRigEditModeSettings* Settings = GetMutableDefault<UControlRigEditModeSettings>();
	Settings->bHideControlShapes = !Settings->bHideControlShapes;
}

void FControlRigEditMode::ResetTransforms(bool bSelectionOnly)
{
	if (UControlRig* ControlRig = GetControlRig(true))
	{
		TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
		TArray<FRigElementKey> ControlsToReset = SelectedRigElements;
		if (!bSelectionOnly)
		{
			TArray<FRigControlElement*> Controls;
			ControlRig->GetControlsInOrder(Controls);
			ControlsToReset.SetNum(0);
			for (const FRigControlElement* Control : Controls)
			{
				ControlsToReset.Add(Control->GetKey());

			}
		}
		bool bHasNonDefaultParent = false;
		TArray<FRigElementKey> Parents;
		for (const FRigElementKey& ControlKey : ControlsToReset)
		{
			FRigElementKey SpaceKey = ControlRig->GetHierarchy()->GetActiveParent(ControlKey);
			Parents.Add(SpaceKey);
			if (SpaceKey != ControlRig->GetHierarchy()->GetDefaultParentKey())
			{
				bHasNonDefaultParent = true;
			}
		}

		FScopedTransaction Transaction(LOCTEXT("HierarchyResetTransforms", "Reset Transforms"));

		for (const FRigElementKey& ControlToReset : ControlsToReset)
		{
			if (ControlToReset.Type == ERigElementType::Control)
			{
				FRigControlElement* ControlElement = ControlRig->FindControl(ControlToReset.Name);
				if (ControlElement && !ControlElement->Settings.bIsTransientControl)
				{
					const FTransform InitialLocalTransform = ControlRig->GetHierarchy()->GetInitialLocalTransform(ControlToReset);
					ControlRig->Modify();
					if (bHasNonDefaultParent == true) //possibly not at default parent so switch to it
					{
						ControlRig->GetHierarchy()->SwitchToDefaultParent(ControlElement->GetKey());
					}
					ControlRig->GetHierarchy()->SetLocalTransform(ControlToReset, InitialLocalTransform);
					if (bHasNonDefaultParent == false)
					{
						ControlRig->ControlModified().Broadcast(ControlRig, ControlElement, EControlRigSetKey::DoNotCare);
					}

					//@helge not sure what to do if the non-default parent
					if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(ControlRig->GetClass()->ClassGeneratedBy))
					{
						Blueprint->Hierarchy->SetLocalTransform(ControlToReset, InitialLocalTransform);
					}
				}
			}
		}

		if (bHasNonDefaultParent == true) //now we have the initial pose setup we need to get the global transforms as specified now then set them in the current parent space
		{
			ControlRig->Evaluate_AnyThread();

			//get global transforms
			TArray<FTransform> GlobalTransforms;
			for (const FRigElementKey& ControlToReset : ControlsToReset)
			{
				FRigControlElement* ControlElement = ControlRig->FindControl(ControlToReset.Name);
				if (ControlElement && !ControlElement->Settings.bIsTransientControl)
				{
					FTransform GlobalTransform = ControlRig->GetHierarchy()->GetGlobalTransform(ControlToReset);
					GlobalTransforms.Add(GlobalTransform);
				}
			}
			//switch back to original parent space
			int32 Index = 0;
			for (const FRigElementKey& ControlToReset : ControlsToReset)
			{
				FRigControlElement* ControlElement = ControlRig->FindControl(ControlToReset.Name);
				if (ControlElement && !ControlElement->Settings.bIsTransientControl)
				{
					ControlRig->GetHierarchy()->SwitchToParent(ControlToReset,Parents[Index]);
					++Index;
				}
			}
			//set global transforms in this space // do it twice since ControlsInOrder is not really always in order
			for (int32 SetHack = 0; SetHack < 2; ++SetHack)
			{
				ControlRig->Evaluate_AnyThread();
				Index = 0;
				for (const FRigElementKey& ControlToReset : ControlsToReset)

				{
					FRigControlElement* ControlElement = ControlRig->FindControl(ControlToReset.Name);
					if (ControlElement && !ControlElement->Settings.bIsTransientControl)
					{
						ControlRig->GetHierarchy()->SetGlobalTransform(ControlToReset, GlobalTransforms[Index]);
						ControlRig->Evaluate_AnyThread();
						++Index;
					}
				}
			}
			//send notifies

			for (const FRigElementKey& ControlToReset : ControlsToReset)
			{
				FRigControlElement* ControlElement = ControlRig->FindControl(ControlToReset.Name);
				if (ControlElement && !ControlElement->Settings.bIsTransientControl)
				{
					ControlRig->ControlModified().Broadcast(ControlRig, ControlElement, EControlRigSetKey::DoNotCare);
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
		if (ActorHitProxy->Actor->IsA<AControlRigShapeActor>())
		{
			for (AControlRigShapeActor* ShapeActor : ShapeActors)
			{
				ShapeActor->SetHovered(ShapeActor == ActorHitProxy->Actor);
			}
		}
	}

	return false;
}

bool FControlRigEditMode::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		ShapeActor->SetHovered(false);
	}

	return false;
}

void FControlRigEditMode::PostUndo()
{
	UControlRig* RuntimeControlRig = GetControlRig(false);
	if (!RuntimeControlRig)
	{
		DestroyShapesActors();
	}
}

void FControlRigEditMode::RecreateControlShapeActors(const TArray<FRigElementKey>& InSelectedElements)
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
		CreateShapeActors(GetWorld());

		USceneComponent* Component = GetHostingSceneComponent();
		if (Component)
		{
			AActor* PreviewActor = Component->GetOwner();

			for (AControlRigShapeActor* ShapeActor : ShapeActors)
			{
				// attach to preview actor, so that we can communicate via relative transfrom from the previewactor
				ShapeActor->AttachToActor(PreviewActor, FAttachmentTransformRules::KeepWorldTransform);

				TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
				ShapeActor->GetComponents(PrimitiveComponents, true);
				for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
				{
					PrimitiveComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FControlRigEditMode::ShapeSelectionOverride);
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

		for (const FRigElementKey& SelectedElement : InSelectedElements)
		{
			if(FRigControlElement* ControlElement = ControlRig->FindControl(SelectedElement.Name))
			{
				OnHierarchyModified(ERigHierarchyNotification::ElementSelected, ControlRig->GetHierarchy(), ControlElement);
			}
		}
	}
}

FControlRigEditMode* FControlRigEditMode::GetEditModeFromWorldContext(UWorld* InWorldContext)
{
	return nullptr;
}

bool FControlRigEditMode::ShapeSelectionOverride(const UPrimitiveComponent* InComponent) const
{
    //Think we only want to do this in regular editor, in the level editor we are driving selection
	if (!IsInLevelEditor())
	{
	AControlRigShapeActor* OwnerActor = Cast<AControlRigShapeActor>(InComponent->GetOwner());
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

void FControlRigEditMode::OnHierarchyModified(ERigHierarchyNotification InNotif, URigHierarchy* InHierarchy, const FRigBaseElement* InElement)
{
	if(bSuspendHierarchyNotifs)
	{
		return;
	}
	
	switch(InNotif)
	{
		case ERigHierarchyNotification::ElementAdded:
		case ERigHierarchyNotification::ElementRemoved:
		case ERigHierarchyNotification::ElementRenamed:
		case ERigHierarchyNotification::HierarchyReset:
		case ERigHierarchyNotification::ControlSettingChanged:
		case ERigHierarchyNotification::ControlShapeTransformChanged:
		{
			// in case the gizmo is turned off, automatically exit gizmo transform edit mode
			if (bIsChangingControlShapeTransform)
			{
				if (!CanChangeControlShapeTransform())
				{
					bIsChangingControlShapeTransform = false;
				} 
			}
			RequestToRecreateControlShapeActors();
			break;
		}
		case ERigHierarchyNotification::ElementSelected:
		case ERigHierarchyNotification::ElementDeselected:
		{
			const FRigElementKey Key = InElement->GetKey();

			switch (InElement->GetType())
			{
				case ERigElementType::Bone:
            	case ERigElementType::Null:
            	case ERigElementType::Curve:
            	case ERigElementType::Control:
            	case ERigElementType::RigidBody:
            	case ERigElementType::Reference:
				{
					const bool bSelected = InNotif == ERigHierarchyNotification::ElementSelected;
					
					// if it's control
					if (Key.Type == ERigElementType::Control)
					{
						FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), IsInLevelEditor() && !GIsTransacting);	
						if (IsInLevelEditor())
						{
							ControlProxy->Modify();
						}
						// users may select gizmo and control rig units, so we have to let them go through both of them if they do
						// first go through gizmo actor
						AControlRigShapeActor* ShapeActor = GetControlShapeFromControlName(Key.Name);
						if (ShapeActor)
						{
							ShapeActor->SetSelected(bSelected);

						}
						if (IsInLevelEditor())
						{
							if (bSelected)
							{
								if (UControlRig* ControlRig = GetControlRig(true))
								{
									if (ControlRig->GetHierarchy()->Find<FRigControlElement>(Key))
									{
										ControlProxy->SelectProxy(Key.Name, true);
									}
								}
							}
							else
							{
								ControlProxy->SelectProxy(Key.Name, false);
							}
						}
					}
					bSelectionChanged = true;
		
					break;
				}
				default:
				{
					ensureMsgf(false, TEXT("Unsupported Type of RigElement: %s"), *Key.ToString());
					break;
				}
			}
		}
		default:
		{
			break;
		}
	}
}

void FControlRigEditMode::OnControlModified(UControlRig* Subject, FRigControlElement* InControlElement, const FRigControlModifiedContext& Context)
{
	//this makes sure the details panel ui get's updated, don't remove
	ControlProxy->ProxyChanged(InControlElement->GetName());

	/*
	FScopedTransaction ScopedTransaction(LOCTEXT("ModifyControlTransaction", "Modify Control"),!GIsTransacting && Context.SetKey != EControlRigSetKey::Never);
	ControlProxy->Modify();
	RecalcPivotTransform();

	if (UControlRig* ControlRig = static_cast<UControlRig*>(Subject))
	{
		FTransform ComponentTransform = GetHostingSceneComponentTransform();
		if (AControlRigShapeActor* const* Actor = GizmoToControlMap.FindKey(InControl.Index))
		{
			TickControlShape(*Actor, ComponentTransform);
		}
	}
	*/
}

void FControlRigEditMode::OnWidgetModeChanged(UE::Widget::EWidgetMode InWidgetMode)
{
	const UControlRigEditModeSettings* Settings = GetDefault<UControlRigEditModeSettings>();
	if (Settings && Settings->bCoordSystemPerWidgetMode)
	{
		TGuardValue<bool> ReentrantGuardSelf(bIsChangingCoordSystem, true);

		FEditorModeTools* ModeManager = GetModeManager();
		int32 WidgetMode = (int32)ModeManager->GetWidgetMode();
		if (WidgetMode >= 0 && WidgetMode < CoordSystemPerWidgetMode.Num())
		{
			ModeManager->SetCoordSystem(CoordSystemPerWidgetMode[WidgetMode]);
		}
	}
}

void FControlRigEditMode::OnCoordSystemChanged(ECoordSystem InCoordSystem)
{
	TGuardValue<bool> ReentrantGuardSelf(bIsChangingCoordSystem, true);

	FEditorModeTools* ModeManager = GetModeManager();
	int32 WidgetMode = (int32)ModeManager->GetWidgetMode();
	ECoordSystem CoordSystem = ModeManager->GetCoordSystem();
	if (WidgetMode >= 0 && WidgetMode < CoordSystemPerWidgetMode.Num())
	{
		CoordSystemPerWidgetMode[WidgetMode] = CoordSystem;
	}
}

bool FControlRigEditMode::CanChangeControlShapeTransform()
{
	if (!IsInLevelEditor())
	{
		TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
		// do not allow multi-select
		if (SelectedRigElements.Num() == 1)
		{
			if (AreRigElementsSelected(FRigElementTypeHelper::ToMask(ERigElementType::Control)))
			{
				if (UControlRig* ControlRig = GetControlRig(true))
				{
					// only enable for a Control with Gizmo enabled and visible
					if (FRigControlElement* ControlElement = ControlRig->GetHierarchy()->Find<FRigControlElement>(SelectedRigElements[0]))
					{
						if (ControlElement->Settings.bShapeEnabled && ControlElement->Settings.bShapeVisible)
						{
							if (AControlRigShapeActor* ShapeActor = GetControlShapeFromControlName(SelectedRigElements[0].Name))
							{
								if (ensure(ShapeActor->IsSelected()))
								{
									return true;
								}
							}
						}
					}
				}
			} 
		}
	}

	return false;
}

void FControlRigEditMode::SetControlShapeTransform(AControlRigShapeActor* ShapeActor, const FTransform& InTransform)
{
	if (UControlRig* ControlRig = GetControlRig(true, ShapeActor->ControlRigIndex))
	{
		ControlRig->SetControlGlobalTransform(ShapeActor->ControlName, InTransform);
	}
}

FTransform FControlRigEditMode::GetControlShapeTransform(AControlRigShapeActor* ShapeActor) const
{
	if (UControlRig* ControlRig = GetControlRig(true, ShapeActor->ControlRigIndex))
	{
		return ControlRig->GetControlGlobalTransform(ShapeActor->ControlName);
	}
	return FTransform::Identity;
}

void FControlRigEditMode::MoveControlShape(AControlRigShapeActor* ShapeActor, const bool bTranslation, FVector& InDrag,
	const bool bRotation, FRotator& InRot, const bool bScale, FVector& InScale, const FTransform& ToWorldTransform,
	bool bUseLocal, bool bCalcLocal, FTransform& InOutLocal)
{
	bool bTransformChanged = false;

	//first case is where we do all controls by the local diff.
	if (bUseLocal)
	{
		if (UControlRig* RuntimeControlRig = GetControlRig(false, ShapeActor->ControlRigIndex))
		{
			UControlRig* InteractionControlRig = GetControlRig(true, ShapeActor->ControlRigIndex);

			FRigControlModifiedContext Context;
			Context.EventName = FRigUnit_BeginExecution::EventName;
			FTransform CurrentLocalTransform = InteractionControlRig->GetControlLocalTransform(ShapeActor->ControlName);
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
				InteractionControlRig->SetControlLocalTransform(ShapeActor->ControlName, CurrentLocalTransform);

				FTransform CurrentTransform  = InteractionControlRig->GetControlGlobalTransform(ShapeActor->ControlName);			// assumes it's attached to actor
				CurrentTransform = ToWorldTransform * CurrentTransform;

				ShapeActor->SetGlobalTransform(CurrentTransform);

				if (RuntimeControlRig->GetInteractionRig() == InteractionControlRig)
				{
					InteractionControlRig->Evaluate_AnyThread();
				}
			}
		}
	}
	if(!bTransformChanged) //not local or doing scale.
	{
		FTransform CurrentTransform = GetControlShapeTransform(ShapeActor) * ToWorldTransform;

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
			if (UControlRig* RuntimeControlRig = GetControlRig(false, ShapeActor->ControlRigIndex))
			{
				UControlRig* InteractionControlRig = GetControlRig(true, ShapeActor->ControlRigIndex);

				FTransform NewTransform = CurrentTransform.GetRelativeTransform(ToWorldTransform);
				FRigControlModifiedContext Context;
				Context.EventName = FRigUnit_BeginExecution::EventName;
				if (bCalcLocal)
				{
					InOutLocal = InteractionControlRig->GetControlLocalTransform(ShapeActor->ControlName);
				}

				bool bPrintPythonCommands = false;
				if (UWorld* World = InteractionControlRig->GetWorld())
				{
					bPrintPythonCommands = World->IsPreviewWorld();
				}
				InteractionControlRig->SetControlGlobalTransform(ShapeActor->ControlName, NewTransform, true, Context, true, bPrintPythonCommands);			// assumes it's attached to actor
				ShapeActor->SetGlobalTransform(CurrentTransform);
				if (bCalcLocal)
				{
					FTransform NewLocal = InteractionControlRig->GetControlLocalTransform(ShapeActor->ControlName);
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
		if (UControlRig* RuntimeControlRig = GetControlRig(false, ShapeActor->ControlRigIndex))
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

void FControlRigEditMode::ChangeControlShapeTransform(AControlRigShapeActor* ShapeActor, const bool bTranslation, FVector& InDrag,
	const bool bRotation, FRotator& InRot, const bool bScale, FVector& InScale, const FTransform& ToWorldTransform)
{
	bool bTransformChanged = false; 

	FTransform CurrentTransform = FTransform::Identity;

	if (UControlRig* ControlRig = GetControlRig(true))
	{
		if (FRigControlElement* ControlElement = ControlRig->GetHierarchy()->Find<FRigControlElement>(FRigElementKey(ShapeActor->ControlName, ERigElementType::Control)))
		{
			CurrentTransform = ControlRig->GetHierarchy()->GetControlShapeTransform(ControlElement, ERigTransformType::CurrentGlobal);
			CurrentTransform = CurrentTransform * ToWorldTransform;
		}
	}

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
		if (UControlRig* RuntimeControlRig = GetControlRig(false, ShapeActor->ControlRigIndex))
		{
			UControlRig* InteractionControlRig = GetControlRig(true, ShapeActor->ControlRigIndex);

			FTransform NewTransform = CurrentTransform.GetRelativeTransform(ToWorldTransform);
			FRigControlModifiedContext Context;

			if (FRigControlElement* ControlElement = InteractionControlRig->GetHierarchy()->Find<FRigControlElement>(FRigElementKey(ShapeActor->ControlName, ERigElementType::Control)))
			{
				// do not setup undo for this first step since it is just used to calculate the local transform
				InteractionControlRig->GetHierarchy()->SetControlShapeTransform(ControlElement, NewTransform, ERigTransformType::CurrentGlobal, false);
				FTransform CurrentLocalShapeTransform = InteractionControlRig->GetHierarchy()->GetControlShapeTransform(ControlElement, ERigTransformType::CurrentLocal);
				// this call should trigger an instance-to-BP update in ControlRigEditor
				InteractionControlRig->GetHierarchy()->SetControlShapeTransform(ControlElement, CurrentLocalShapeTransform, ERigTransformType::InitialLocal, true);

				FTransform MeshTransform = FTransform::Identity;
				FTransform ShapeTransform = CurrentLocalShapeTransform;

				if (UControlRig* ControlRig = GetControlRig(true))
				{
					if (const FControlRigShapeDefinition* Gizmo = UControlRigShapeLibrary::GetShapeByName(ControlElement->Settings.ShapeName, GetControlRig(true)->GetShapeLibraries()))
					{
						MeshTransform = Gizmo->Transform;
					}
				}

				ShapeActor->StaticMeshComponent->SetRelativeTransform(MeshTransform * ShapeTransform);
			}
		}
	} 
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

bool FControlRigEditMode::ModeSupportedByShapeActor(const AControlRigShapeActor* ShapeActor, UE::Widget::EWidgetMode InMode) const
{
	if (UControlRig* ControlRig = GetControlRig(true, ShapeActor->ControlRigIndex))
	{
		const FRigControlElement* ControlElement = ControlRig->FindControl(ShapeActor->ControlName);
		if (ControlElement)
		{
			if (bIsChangingControlShapeTransform)
			{
				return true;
			}

			if (IsSupportedControlType(ControlElement->Settings.ControlType))
			{
				switch (InMode)
				{
					case UE::Widget::WM_None:
						return true;
					case UE::Widget::WM_Rotate:
					{
						switch (ControlElement->Settings.ControlType)
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
					case UE::Widget::WM_Translate:
					{
						switch (ControlElement->Settings.ControlType)
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
					case UE::Widget::WM_Scale:
					{
						switch (ControlElement->Settings.ControlType)
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
					case UE::Widget::WM_TranslateRotateZ:
					{
						switch (ControlElement->Settings.ControlType)
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

void FControlRigEditMode::TickControlShape(AControlRigShapeActor* ShapeActor, const FTransform& ComponentTransform)
{
	UControlRigEditModeSettings* Settings = GetMutableDefault<UControlRigEditModeSettings>();
	if (ShapeActor)
	{
		if (UControlRig* ControlRig = GetControlRig(true, ShapeActor->ControlRigIndex))
		{
			const FTransform Transform = ControlRig->GetControlGlobalTransform(ShapeActor->ControlName);
			ShapeActor->SetActorTransform(Transform * ComponentTransform);

			if (FRigControlElement* ControlElement = ControlRig->FindControl(ShapeActor->ControlName))
			{
				ShapeActor->SetShapeColor(ControlElement->Settings.ShapeColor);
				ShapeActor->SetIsTemporarilyHiddenInEditor(!ControlElement->Settings.bShapeVisible || Settings->bHideControlShapes);
				if (!IsInLevelEditor()) //don't change this in level editor otherwise we can never select it
				{
					ShapeActor->SetSelectable(ControlElement->Settings.bShapeVisible && !Settings->bHideControlShapes && ControlElement->Settings.bAnimatable);
				}
			}
		}
	}
}

AControlRigShapeActor* FControlRigEditMode::GetControlShapeFromControlName(const FName& ControlName) const
{
	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		if (ShapeActor->ControlName == ControlName)
		{
			return ShapeActor;
		}
	}

	return nullptr;
}

void FControlRigEditMode::AddControlRig(UControlRig* InControlRig)
{
	RuntimeControlRigs.AddUnique(InControlRig);

	InControlRig->PostInitInstanceIfRequired();
	InControlRig->GetHierarchy()->OnModified().RemoveAll(this);
	InControlRig->GetHierarchy()->OnModified().AddSP(this, &FControlRigEditMode::OnHierarchyModified);

	OnControlRigAddedOrRemovedDelegate.Broadcast(InControlRig, true);
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
		OnControlRigAddedOrRemovedDelegate.Broadcast(InControlRig, false);
		RuntimeControlRigs[Index]->ControlModified().RemoveAll(this);
		RuntimeControlRigs[Index]->GetHierarchy()->OnModified().RemoveAll(this);
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

bool FControlRigEditMode::CreateShapeActors(UWorld* World)
{
	DestroyShapesActors();

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

		TArray<FRigControlElement*> Controls = ControlRig->AvailableControls();
		const TArray<TSoftObjectPtr<UControlRigShapeLibrary>> ShapeLibraries = ControlRig->GetShapeLibraries();

		for (FRigControlElement* ControlElement : Controls)
		{
			if (!ControlElement->Settings.bShapeEnabled)
			{
				continue;
			}
			if (IsSupportedControlType(ControlElement->Settings.ControlType))
			{
				FControlShapeActorCreationParam Param;
				Param.ManipObj = ControlRig;
				Param.ControlRigIndex = ControlRigIndex;
				Param.ControlName = ControlElement->GetName();
				Param.SpawnTransform = ControlRig->GetControlGlobalTransform(ControlElement->GetName());
				Param.ShapeTransform = ControlRig->GetHierarchy()->GetControlShapeTransform(ControlElement, ERigTransformType::CurrentLocal);
				Param.bSelectable = ControlElement->Settings.bAnimatable;

				if (const FControlRigShapeDefinition* ShapeDef = UControlRigShapeLibrary::GetShapeByName(ControlElement->Settings.ShapeName, ShapeLibraries))
				{
					Param.MeshTransform = ShapeDef->Transform;
					Param.StaticMesh = ShapeDef->StaticMesh;
					Param.Material = ShapeDef->Library->DefaultMaterial;
					Param.ColorParameterName = ShapeDef->Library->MaterialColorParameter;
				}

				Param.Color = ControlElement->Settings.ShapeColor;

				AControlRigShapeActor* ShapeActor = FControlRigShapeHelper::CreateDefaultShapeActor(World, Param);
				if (ShapeActor)
				{
					ShapeActors.Add(ShapeActor);
				}
			}
		}
	}

	WorldPtr = World;
	OnWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddSP(this, &FControlRigEditMode::OnWorldCleanup);
	return (ShapeActors.Num() > 0);
}

void FControlRigEditMode::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	// if world gets cleaned up first, we destroy gizmo actors
	if (WorldPtr == World)
	{
		DestroyShapesActors();
	}
}

void FControlRigEditMode::DestroyShapesActors()
{
	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		UWorld* World = ShapeActor->GetWorld();
		if (World)
		{
			World->DestroyActor(ShapeActor);
		}
	}
	ShapeActors.Reset();

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
	FTransform ComponentTransform = FTransform::Identity;
	if(IsInLevelEditor())
	{
		ComponentTransform = GetHostingSceneComponentTransform();
	}
	
	for(AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		TickControlShape(ShapeActor, ComponentTransform);
	}

}
void FControlRigEditMode::SetOnlySelectRigControls(bool Val)
{
	UControlRigEditModeSettings* Settings = GetMutableDefault<UControlRigEditModeSettings>();
	Settings->bOnlySelectRigControls = Val;
}

bool FControlRigEditMode::GetOnlySelectRigControls()const
{
	const UControlRigEditModeSettings* Settings = GetDefault<UControlRigEditModeSettings>();
	return Settings->bOnlySelectRigControls;
}




#undef LOCTEXT_NAMESPACE
