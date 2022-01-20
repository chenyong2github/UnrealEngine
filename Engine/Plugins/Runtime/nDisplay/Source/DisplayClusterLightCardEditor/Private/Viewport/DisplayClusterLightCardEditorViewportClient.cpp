// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorViewportClient.h"

#include "DisplayClusterRootActor.h"

#include "AudioDevice.h"
#include "EngineUtils.h"
#include "PreviewScene.h"
#include "UnrealEdGlobals.h"
#include "ScopedTransaction.h"
#include "SDisplayClusterLightCardEditor.h"
#include "UnrealWidget.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "Kismet/GameplayStatics.h"
#include "Slate/SceneViewport.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditorViewportClient"

FDisplayClusterLightCardEditorViewportClient::FDisplayClusterLightCardEditorViewportClient(FAdvancedPreviewScene& InPreviewScene,
                                                           const TWeakPtr<SEditorViewport>& InEditorViewportWidget,
                                                           TWeakPtr<SDisplayClusterLightCardEditor> InLightCardEditor) :
	FEditorViewportClient(nullptr, &InPreviewScene, InEditorViewportWidget),
	PreviewActorBounds(ForceInitToZero)
{
	check (InLightCardEditor.IsValid());
	
	LightCardEditorPtr = InLightCardEditor;
	
	SelectedActor = nullptr;
	bDraggingActor = false;
	ScopedTransaction = nullptr;
	
	// Setup defaults for the common draw helper.
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = false;
	DrawHelper.PerspectiveGridSize = HALF_WORLD_MAX1;

	check(Widget);
	Widget->SetSnapEnabled(true);
	
	ShowWidget(true);

	SetViewMode(VMI_Lit);
	
	ViewportType = LVT_Perspective;
	bSetListenerPosition = false;
	SetRealtime(true);
	SetShowStats(true);

	//This seems to be needed to get the correct world time in the preview.
	SetIsSimulateInEditorViewport(true);

	ResetCamera();
	
	UpdatePreviewActor(LightCardEditorPtr.Pin()->GetActiveRootActor().Get());
}

FDisplayClusterLightCardEditorViewportClient::~FDisplayClusterLightCardEditorViewportClient()
{
	EndTransaction();
}

FLinearColor FDisplayClusterLightCardEditorViewportClient::GetBackgroundColor() const
{
	return FLinearColor::Gray;
}

void FDisplayClusterLightCardEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// Tick the preview scene world.
	if (!GIntraFrameDebuggingGameThread)
	{
		// Allow full tick only if preview simulation is enabled and we're not currently in an active SIE or PIE session
		if (GEditor->PlayWorld == nullptr && !GEditor->bIsSimulatingInEditor)
		{
			PreviewScene->GetWorld()->Tick(IsRealtime() ? LEVELTICK_All : LEVELTICK_TimeOnly, DeltaSeconds);
		}
		else
		{
			PreviewScene->GetWorld()->Tick(IsRealtime() ? LEVELTICK_ViewportsOnly : LEVELTICK_TimeOnly, DeltaSeconds);
		}
	}
}

void FDisplayClusterLightCardEditorViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	FEditorViewportClient::Draw(InViewport, Canvas);
}

UE::Widget::EWidgetMode FDisplayClusterLightCardEditorViewportClient::GetWidgetMode() const
{
	if (IsActorSelected())
	{
		return FEditorViewportClient::GetWidgetMode();
	}

	return UE::Widget::WM_None;
}

FVector FDisplayClusterLightCardEditorViewportClient::GetWidgetLocation() const
{
	if (IsActorSelected())
	{
		return SelectedActor->GetActorLocation();
	}

	return FEditorViewportClient::GetWidgetLocation();
}

bool FDisplayClusterLightCardEditorViewportClient::InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event,
                                            float AmountDepressed, bool bGamepad)
{
	return FEditorViewportClient::InputKey(InViewport, ControllerId, Key, Event, AmountDepressed, bGamepad);
}

bool FDisplayClusterLightCardEditorViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag,
                                                    FRotator& Rot, FVector& Scale)
{
	if (IsActorSelected() && bDraggingActor)
	{
		GEditor->ApplyDeltaToActor(SelectedActor.Get(), true, &Drag, &Rot, &Scale);
		return true;
	}

	return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
}

void FDisplayClusterLightCardEditorViewportClient::TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget,
	bool bNudge)
{
	if (!bDraggingActor && bIsDraggingWidget && InInputState.IsLeftMouseButtonPressed() && IsActorSelected())
	{
		GEditor->DisableDeltaModification(true);
		{
			// The pivot location won't update properly and the actor will rotate / move around the original selection origin
			// so update it here to fix that.
			GUnrealEd->UpdatePivotLocationForSelection();
			GUnrealEd->SetPivotMovedIndependently(false);
		}

		BeginTransaction(NSLOCTEXT("LogicDriverPreview", "ModifyPreviewActor", "Modify a Preview Actor"));
		bDraggingActor = true;
	}
	FEditorViewportClient::TrackingStarted(InInputState, bIsDraggingWidget, bNudge);
}

void FDisplayClusterLightCardEditorViewportClient::TrackingStopped()
{
	bDraggingActor = false;
	EndTransaction();

	if (IsActorSelected())
	{
		GEditor->DisableDeltaModification(false);
	}
	
	FEditorViewportClient::TrackingStopped();
}

void FDisplayClusterLightCardEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event,
                                                uint32 HitX, uint32 HitY)
{
	if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		HActor* HitActor = static_cast<HActor*>(HitProxy);
		SelectActor(HitActor->Actor);
		return;
	}

	SelectActor(nullptr);
	
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

void FDisplayClusterLightCardEditorViewportClient::SetSceneViewport(TSharedPtr<FSceneViewport> InViewport)
{
	SceneViewportPtr = InViewport;
}

void FDisplayClusterLightCardEditorViewportClient::SelectActor(AActor* NewActor)
{
}

void FDisplayClusterLightCardEditorViewportClient::ResetSelection()
{
	SelectActor(nullptr);
	SetWidgetMode(UE::Widget::WM_None);
}

void FDisplayClusterLightCardEditorViewportClient::ResetCamera()
{
	ToggleOrbitCamera(true);
	SetViewLocationForOrbiting(PreviewActorBounds.Origin);

	// TODO: Better define defaults.
	SetViewLocation(EditorViewportDefs::DefaultPerspectiveViewLocation + FVector(0.f, -400.f, 0.f));
	SetViewRotation(EditorViewportDefs::DefaultPerspectiveViewRotation + FRotator(0.f, 180.f, 0.f));

	Invalidate();
}

void FDisplayClusterLightCardEditorViewportClient::UpdatePreviewActor(
	ADisplayClusterRootActor* RootActor)
{
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	check (PreviewWorld);

	if (SpawnedRootActor.IsValid() && (!RootActor || !PreviewWorld->ContainsActor(RootActor)))
	{
		PreviewWorld->EditorDestroyActor(SpawnedRootActor.Get(), false);
		SpawnedRootActor.Reset();
	}
	
	if (RootActor)
	{
		const FVector SpawnLocation = FVector::ZeroVector;
		const FRotator SpawnRotation = FRotator::ZeroRotator;
		
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.bNoFail = true;
		SpawnInfo.ObjectFlags = RF_Transient|RF_Transactional;
		SpawnInfo.Template = RootActor;
		
		SpawnedRootActor = CastChecked<ADisplayClusterRootActor>(PreviewWorld->SpawnActor(RootActor->GetClass(),
			&SpawnLocation, &SpawnRotation, MoveTemp(SpawnInfo)));

		// Compute actor bounds as the sum of its visible parts
		PreviewActorBounds = FBoxSphereBounds(ForceInitToZero);
		for (UActorComponent* Component : RootActor->GetComponents())
		{
			// Aggregate primitive components that either have collision enabled or are otherwise visible components in-game
			if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
			{
				if (PrimComp->IsRegistered() && (!PrimComp->bHiddenInGame || PrimComp->IsCollisionEnabled()) && PrimComp->Bounds.SphereRadius < HALF_WORLD_MAX)
				{
					PreviewActorBounds = PreviewActorBounds + PrimComp->Bounds;
				}
			}
		}

		SpawnedRootActor->UpdatePreviewComponents();
	}
}

bool FDisplayClusterLightCardEditorViewportClient::GetShowGrid() const
{
	return IsSetShowGridChecked();
}

void FDisplayClusterLightCardEditorViewportClient::ToggleShowGrid()
{
	SetShowGrid();
}

void FDisplayClusterLightCardEditorViewportClient::BeginTransaction(const FText& Description)
{
	if (!ScopedTransaction)
	{
		ScopedTransaction = new FScopedTransaction(Description);
	}
}

void FDisplayClusterLightCardEditorViewportClient::EndTransaction()
{
	if (ScopedTransaction)
	{
		delete ScopedTransaction;
		ScopedTransaction = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
