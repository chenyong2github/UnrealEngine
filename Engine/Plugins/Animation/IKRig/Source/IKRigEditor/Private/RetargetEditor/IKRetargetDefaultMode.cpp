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

	// deselect all
	SetSelectedComponent(nullptr);

	// update offsets on preview meshes
	Controller->AddOffsetAndUpdatePreviewMeshPosition(FVector::ZeroVector, Controller->SourceSkelMeshComponent);
	Controller->AddOffsetAndUpdatePreviewMeshPosition(FVector::ZeroVector, Controller->TargetSkelMeshComponent);

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
	const bool bTranslating = CheckMode == UE::Widget::EWidgetMode::WM_Translate;
	return bTranslating && IsValid(SelectedComponent);
}

FVector FIKRetargetDefaultMode::GetWidgetLocation() const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FVector::ZeroVector; 
	}
	
	if (SelectedComponent.IsNull())
	{
		return FVector::ZeroVector; // shouldn't get here
	}

	return SelectedComponent->GetComponentTransform().GetLocation();
}

bool FIKRetargetDefaultMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	// did we select an actor in the viewport?
	const bool bLeftButtonClicked = Click.GetKey() == EKeys::LeftMouseButton;
	const bool bHitActor = HitProxy && HitProxy->IsA(HActor::StaticGetType());
	if (bLeftButtonClicked && bHitActor)
	{
		const HActor* ActorProxy = static_cast<HActor*>(HitProxy);
		SetSelectedComponent(const_cast<UPrimitiveComponent*>(ActorProxy->PrimComponent));
		return true;
	}

	SetSelectedComponent(nullptr);
	return false;
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
	if (bTranslating && IsValid(SelectedComponent))
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
	if (!(bIsTranslating && IsValid(SelectedComponent)))
	{
		return false; // not handled
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	if(InViewportClient->GetWidgetMode() != UE::Widget::WM_Translate)
	{
		return false;
	}

	Controller->AddOffsetAndUpdatePreviewMeshPosition(InDrag, SelectedComponent);
	
	return true;
}

bool FIKRetargetDefaultMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	if (SelectedComponent.IsNull())
	{
		return false;
	}

	InMatrix = SelectedComponent->GetComponentTransform().ToMatrixNoScale().RemoveTranslation();
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

	// allow selection of meshes in this mode
	Controller->Editor.Pin()->GetPersonaToolkit()->GetPreviewScene()->SetAllowMeshHitProxies(true);
	Controller->SourceSkelMeshComponent->bSelectable = true;
	Controller->TargetSkelMeshComponent->bSelectable = true;
	Controller->AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::RunRetarget);

	// deselect when entering mode
	SetSelectedComponent(nullptr);
}

void FIKRetargetDefaultMode::Exit()
{
	IPersonaEditMode::Exit();

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	// disable selection in other modes
	Controller->Editor.Pin()->GetPersonaToolkit()->GetPreviewScene()->SetAllowMeshHitProxies(false);
	Controller->SourceSkelMeshComponent->bSelectable = false;
	Controller->TargetSkelMeshComponent->bSelectable = false;
	
	// deselect all
	SetSelectedComponent(nullptr);
}

bool FIKRetargetDefaultMode::ComponentSelectionOverride(const UPrimitiveComponent* InComponent) const
{
	return InComponent == SelectedComponent;
}

void FIKRetargetDefaultMode::SetSelectedComponent(UPrimitiveComponent* InComponent)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	SelectedComponent = InComponent;

	Controller->SourceSkelMeshComponent->PushSelectionToProxy();
	Controller->TargetSkelMeshComponent->PushSelectionToProxy();
	
	Controller->SourceSkelMeshComponent->MarkRenderStateDirty();
	Controller->TargetSkelMeshComponent->MarkRenderStateDirty();

	// when clicking in empty space, show global details
	if (SelectedComponent == nullptr)
	{
		Controller->ChainsView->ClearSelection();
		Controller->DetailsView->SetObject(Controller->AssetController->GetAsset());
	}
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
}

void FIKRetargetDefaultMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

#undef LOCTEXT_NAMESPACE
