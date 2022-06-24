// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetDefaultMode.h"

#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "EngineUtils.h"
#include "IKRigDebugRendering.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/SIKRetargetChainMapList.h"


#define LOCTEXT_NAMESPACE "IKRetargetDefaultMode"

FName FIKRetargetDefaultMode::ModeName("IKRetargetAssetDefaultMode");

bool FIKRetargetDefaultMode::GetCameraTarget(FSphere& OutTarget) const
{
	// target skeletal mesh
	if (const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin())
	{
		if (Controller->SourceSkelMeshComponent)
		{
			OutTarget = Controller->SourceSkelMeshComponent->Bounds.GetSphere();
			return true;
		}
	}
	
	return false;
}

IPersonaPreviewScene& FIKRetargetDefaultMode::GetAnimPreviewScene() const
{
	return *static_cast<IPersonaPreviewScene*>(static_cast<FAssetEditorModeManager*>(Owner)->GetPreviewScene());
}

void FIKRetargetDefaultMode::GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const
{
}

void FIKRetargetDefaultMode::Initialize()
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	// register selection callback overrides
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	PrimitiveComponents.Add(Controller->SourceSkelMeshComponent);
	PrimitiveComponents.Add(Controller->TargetSkelMeshComponent);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		PrimitiveComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FIKRetargetDefaultMode::ComponentSelectionOverride);
	}

	// update offsets on preview meshes
	Controller->AddOffsetToMeshComponent(FVector::ZeroVector, Controller->SourceSkelMeshComponent);
	Controller->AddOffsetToMeshComponent(FVector::ZeroVector, Controller->TargetSkelMeshComponent);

	bIsInitialized = true;
}

void FIKRetargetDefaultMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);
}

bool FIKRetargetDefaultMode::AllowWidgetMove()
{
	return false;
}

bool FIKRetargetDefaultMode::ShouldDrawWidget() const
{
	return UsesTransformWidget(CurrentWidgetMode);
}

bool FIKRetargetDefaultMode::UsesTransformWidget() const
{
	return UsesTransformWidget(CurrentWidgetMode);
}

bool FIKRetargetDefaultMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	const bool bTranslating = CheckMode == UE::Widget::EWidgetMode::WM_Translate;
	return bTranslating && IsValid(Controller->GetSelectedMesh());
}

FVector FIKRetargetDefaultMode::GetWidgetLocation() const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FVector::ZeroVector; 
	}
	
	if (!Controller->GetSelectedMesh())
	{
		return FVector::ZeroVector; // shouldn't get here
	}

	return Controller->GetSelectedMesh()->GetComponentTransform().GetLocation();
}

bool FIKRetargetDefaultMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	const bool bLeftButtonClicked = Click.GetKey() == EKeys::LeftMouseButton;
	constexpr bool bFromHierarchy = false;
	
	// did we click on an actor in the viewport?
	const bool bHitActor = HitProxy && HitProxy->IsA(HActor::StaticGetType());
	if (bLeftButtonClicked && bHitActor)
	{
		const HActor* ActorProxy = static_cast<HActor*>(HitProxy);
		Controller->SetSelectedMesh(const_cast<UPrimitiveComponent*>(ActorProxy->PrimComponent));
		return true;
	}

	// did we click on a bone in the viewport?
	const bool bHitBone = HitProxy && HitProxy->IsA(HPersonaBoneHitProxy::StaticGetType());
	if (bLeftButtonClicked && bHitBone)
	{
		const HPersonaBoneHitProxy* BoneProxy = static_cast<HPersonaBoneHitProxy*>(HitProxy);
		const TArray<FName> BoneNames{BoneProxy->BoneName};
		
		const bool bCtrlOrShiftHeld = Click.IsControlDown() || Click.IsShiftDown();
		const EBoneSelectionEdit EditMode = bCtrlOrShiftHeld ? EBoneSelectionEdit::Add : EBoneSelectionEdit::Replace;
		
		Controller->EditBoneSelection(BoneNames, EditMode, bFromHierarchy);
		
		return true;
	}

	// did we click in empty space in viewport?
	if (!(bHitActor || bHitBone))
	{
		// deselect all meshes, bones, chains and update details view
		Controller->ClearSelection();
	}
	
	return true;
}

bool FIKRetargetDefaultMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bIsTranslating = false;

	// not manipulating any widget axes, so stop tracking
	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	if (CurrentAxis == EAxisList::None)
	{
		return false; 
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; // invalid editor state
	}

	const bool bTranslating = InViewportClient->GetWidgetMode() == UE::Widget::EWidgetMode::WM_Translate;
	if (bTranslating && IsValid(Controller->GetSelectedMesh()))
	{
		bIsTranslating = true;
		GEditor->BeginTransaction(LOCTEXT("MovePreviewMesh", "Move Preview Mesh"));
		Controller->AssetController->GetAsset()->Modify();
		return true;
	}

	return false;
}

bool FIKRetargetDefaultMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	GEditor->EndTransaction();
	bIsTranslating = false;
	return true;
}

bool FIKRetargetDefaultMode::InputDelta(
	FEditorViewportClient* InViewportClient,
	FViewport* InViewport,
	FVector& InDrag,
	FRotator& InRot,
	FVector& InScale)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	if (!(bIsTranslating && IsValid(Controller->GetSelectedMesh())))
	{
		return false; // not handled
	}

	if(InViewportClient->GetWidgetMode() != UE::Widget::WM_Translate)
	{
		return false;
	}

	Controller->AddOffsetToMeshComponent(InDrag, Controller->GetSelectedMesh());
	
	return true;
}

bool FIKRetargetDefaultMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	if (!Controller->GetSelectedMesh())
	{
		return false;
	}

	InMatrix = Controller->GetSelectedMesh()->GetComponentTransform().ToMatrixNoScale().RemoveTranslation();
	return true;
}

bool FIKRetargetDefaultMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

void FIKRetargetDefaultMode::Enter()
{
	IPersonaEditMode::Enter();

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	// record which skeleton is being viewed/edited
	SkeletonMode = Controller->GetSkeletonMode();

	// allow selection of meshes in this mode
	Controller->Editor.Pin()->GetPersonaToolkit()->GetPreviewScene()->SetAllowMeshHitProxies(true);
	Controller->SourceSkelMeshComponent->bSelectable = true;
	Controller->TargetSkelMeshComponent->bSelectable = true;
}

void FIKRetargetDefaultMode::Exit()
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	// editor can be closed while in edit mode
	if (Controller->Editor.IsValid())
	{
		// disable selection in other modes
		Controller->Editor.Pin()->GetPersonaToolkit()->GetPreviewScene()->SetAllowMeshHitProxies(false);
		Controller->SourceSkelMeshComponent->bSelectable = false;
		Controller->TargetSkelMeshComponent->bSelectable = false;
	}

	IPersonaEditMode::Exit();
}

bool FIKRetargetDefaultMode::ComponentSelectionOverride(const UPrimitiveComponent* InComponent) const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	return InComponent == Controller->GetSelectedMesh();
}

UDebugSkelMeshComponent* FIKRetargetDefaultMode::GetCurrentlyEditedMesh() const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return nullptr; 
	}
	
	return SkeletonMode == EIKRetargetSkeletonMode::Source ? Controller->SourceSkelMeshComponent : Controller->TargetSkelMeshComponent;
}

void FIKRetargetDefaultMode::ApplyOffsetToMeshTransform(const FVector& Offset, USceneComponent* Component)
{
	constexpr bool bSweep = false;
	constexpr FHitResult* OutSweepHitResult = nullptr;
	constexpr ETeleportType Teleport = ETeleportType::ResetPhysics;
	Component->SetWorldLocation(Offset, bSweep, OutSweepHitResult, Teleport);
}

void FIKRetargetDefaultMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
	
	CurrentWidgetMode = ViewportClient->GetWidgetMode();

	// ensure selection callbacks have been generated
	if (!bIsInitialized)
	{
		Initialize();
	}

	// update skeleton drawing mode
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (Controller.IsValid())
	{
		const bool bEditingSource = Controller->GetSkeletonMode() == EIKRetargetSkeletonMode::Source;
		Controller->SourceSkelMeshComponent->SkeletonDrawMode = bEditingSource ? ESkeletonDrawMode::Default : ESkeletonDrawMode::GreyedOut;
		Controller->TargetSkelMeshComponent->SkeletonDrawMode = !bEditingSource ? ESkeletonDrawMode::Default : ESkeletonDrawMode::GreyedOut;
	}
}

void FIKRetargetDefaultMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

#undef LOCTEXT_NAMESPACE
