// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorViewportClient.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterProjectionStrings.h"
#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterPreviewComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"

#include "AudioDevice.h"
#include "EngineUtils.h"
#include "EditorModes.h"
#include "EditorModeManager.h"
#include "PreviewScene.h"
#include "UnrealEdGlobals.h"
#include "ScopedTransaction.h"
#include "SDisplayClusterLightCardEditor.h"
#include "UnrealWidget.h"
#include "RayTracingDebugVisualizationMenuCommands.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "Kismet/GameplayStatics.h"
#include "Slate/SceneViewport.h"
#include "Widgets/Docking/SDockTab.h"
#include "CustomEditorStaticScreenPercentage.h"
#include "LegacyScreenPercentageDriver.h"
#include "EngineModule.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CameraController.h"


#include "Renderer/Private/SceneRendering.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditorViewportClient"

FDisplayClusterLightCardEditorViewportClient::FDisplayClusterLightCardEditorViewportClient(FAdvancedPreviewScene& InPreviewScene,
                                                           const TWeakPtr<SEditorViewport>& InEditorViewportWidget,
                                                           TWeakPtr<SDisplayClusterLightCardEditor> InLightCardEditor) :
	FEditorViewportClient(nullptr, &InPreviewScene, InEditorViewportWidget)
{
	check (InLightCardEditor.IsValid());
	
	LightCardEditorPtr = InLightCardEditor;
	
	MeshProjectionRenderer = MakeShared<FDisplayClusterMeshProjectionRenderer>();
	MeshProjectionRenderer->ActorSelectedDelegate = FDisplayClusterMeshProjectionRenderer::FSelection::CreateRaw(this, &FDisplayClusterLightCardEditorViewportClient::IsLightCardSelected);

	bDraggingActor = false;
	ScopedTransaction = nullptr;
	
	// Setup defaults for the common draw helper.
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = false;
	DrawHelper.PerspectiveGridSize = HALF_WORLD_MAX1;

	EngineShowFlags.SetSelectionOutline(true);

	check(Widget);
	Widget->SetSnapEnabled(true);
	
	ShowWidget(true);

	SetViewMode(VMI_Unlit);
	
	ViewportType = LVT_Perspective;
	bSetListenerPosition = false;
	bUseNumpadCameraControl = false;
	SetRealtime(true);
	SetShowStats(true);

	ProjectionFOVs.AddZeroed(2);
	ProjectionFOVs[(int32)EDisplayClusterMeshProjectionType::Perspective] = 90.0f;
	ProjectionFOVs[(int32)EDisplayClusterMeshProjectionType::Azimuthal] = 130.0f;

	//This seems to be needed to get the correct world time in the preview.
	SetIsSimulateInEditorViewport(true);
	
	UpdatePreviewActor(LightCardEditorPtr.Pin()->GetActiveRootActor().Get());
}

FDisplayClusterLightCardEditorViewportClient::~FDisplayClusterLightCardEditorViewportClient()
{
	EndTransaction();
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
}

FLinearColor FDisplayClusterLightCardEditorViewportClient::GetBackgroundColor() const
{
	return FLinearColor::Gray;
}

void FDisplayClusterLightCardEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// Camera position is locked to a specific location
	FVector Location = FVector::ZeroVector;
	if (ProjectionOriginComponent.IsValid())
	{
		Location = ProjectionOriginComponent->GetComponentLocation();
	}

	SetViewLocation(Location);

	// View rotation is also locked for the azimuthal projection
	if (ProjectionMode != EDisplayClusterMeshProjectionType::Perspective)
	{
		SetViewRotation(FVector::UpVector.Rotation());
	}

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

	if (RootActorProxy.IsValid() && RootActorLevelInstance.IsValid())
	{
		// Pass the preview render targets from the level instance root actor to the preview root actor
		UDisplayClusterConfigurationData* Config = RootActorLevelInstance->GetConfigData();

		for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& NodePair : Config->Cluster->Nodes)
		{
			const UDisplayClusterConfigurationClusterNode* Node = NodePair.Value;
			for (const TPair<FString, UDisplayClusterConfigurationViewport*>& ViewportPair : Node->Viewports)
			{
				UDisplayClusterPreviewComponent* LevelInstancePreviewComp = RootActorLevelInstance->GetPreviewComponent(NodePair.Key, ViewportPair.Key);
				UDisplayClusterPreviewComponent* PreviewComp = RootActorProxy->GetPreviewComponent(NodePair.Key, ViewportPair.Key);

				if (PreviewComp && LevelInstancePreviewComp)
				{
					PreviewComp->SetOverrideTexture(LevelInstancePreviewComp->GetRenderTargetTexture());
				}
			}
		}
	}
}

void FDisplayClusterLightCardEditorViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	FViewport* ViewportBackup = Viewport;
	Viewport = InViewport ? InViewport : Viewport;

	UWorld* World = GetWorld();
	FGameTime Time;
	if (!World || (GetScene() != World->Scene) || UseAppTime()) 
	{
		Time = FGameTime::GetTimeSinceAppStart();
	}
	else
	{
		Time = World->GetTime();
	}

	FEngineShowFlags UseEngineShowFlags = EngineShowFlags;
	if (OverrideShowFlagsFunc)
	{
		OverrideShowFlagsFunc(UseEngineShowFlags);
	}

	// Setup a FSceneViewFamily/FSceneView for the viewport.
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Canvas->GetRenderTarget(),
		GetScene(),
		UseEngineShowFlags)
		.SetTime(Time)
		.SetRealtimeUpdate(IsRealtime() && FSlateThrottleManager::Get().IsAllowingExpensiveTasks())
	);

	ViewFamily.DebugDPIScale = GetDPIScale();
	ViewFamily.bIsHDR = Viewport->IsHDRViewport();

	ViewFamily.EngineShowFlags = UseEngineShowFlags;
	ViewFamily.EngineShowFlags.CameraInterpolation = 0;
	ViewFamily.EngineShowFlags.SetScreenPercentage(false);

	ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(FSceneViewExtensionContext(InViewport));

	for (auto ViewExt : ViewFamily.ViewExtensions)
	{
		ViewExt->SetupViewFamily(ViewFamily);
	}

	ViewFamily.ViewMode = VMI_Unlit;

	EngineShowFlagOverride(ESFIM_Editor, ViewFamily.ViewMode, ViewFamily.EngineShowFlags, false);
	EngineShowFlagOrthographicOverride(IsPerspective(), ViewFamily.EngineShowFlags);

	ViewFamily.ExposureSettings = ExposureSettings;

	// Setup the screen percentage and upscaling method for the view family.
	{
		checkf(ViewFamily.GetScreenPercentageInterface() == nullptr,
			TEXT("Some code has tried to set up an alien screen percentage driver, that could be wrong if not supported very well by the RHI."));

		if (SupportsLowDPIPreview() && IsLowDPIPreview() && ViewFamily.SupportsScreenPercentage())
		{
			ViewFamily.SecondaryViewFraction = GetDPIDerivedResolutionFraction();
		}
	}

	FSceneView* View = nullptr;

	View = CalcSceneView(&ViewFamily, INDEX_NONE);
	SetupViewForRendering(ViewFamily,*View);

	FSlateRect SafeFrame;
	View->CameraConstrainedViewRect = View->UnscaledViewRect;
	if (CalculateEditorConstrainedViewRect(SafeFrame, Viewport, Canvas->GetDPIScale()))
	{
		View->CameraConstrainedViewRect = FIntRect(SafeFrame.Left, SafeFrame.Top, SafeFrame.Right, SafeFrame.Bottom);
	}

	{
		// If a screen percentage interface was not set by one of the view extension, then set the legacy one.
		if (ViewFamily.GetScreenPercentageInterface() == nullptr)
		{
			float GlobalResolutionFraction = 1.0f;

			if (SupportsPreviewResolutionFraction() && ViewFamily.SupportsScreenPercentage())
			{
				GlobalResolutionFraction = GetDefaultPrimaryResolutionFractionTarget();

				// Force screen percentage's engine show flag to be turned on for preview screen percentage.
				ViewFamily.EngineShowFlags.ScreenPercentage = (GlobalResolutionFraction != 1.0);
			}

			// In editor viewport, we ignore r.ScreenPercentage and FPostProcessSettings::ScreenPercentage by design.
			ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, GlobalResolutionFraction));
		}

		check(ViewFamily.GetScreenPercentageInterface() != nullptr);
	}

	Canvas->Clear(FLinearColor::Black);

	FSceneViewInitOptions ViewInitOptions;
	GetSceneViewInitOptions(ViewInitOptions);

	MeshProjectionRenderer->Render(Canvas, GetScene(), ViewInitOptions, EngineShowFlags, ProjectionMode);

	if (View)
	{
		DrawCanvas(*Viewport, *View, *Canvas);
	}

	// Remove temporary debug lines.
	// Possibly a hack. Lines may get added without the scene being rendered etc.
	if (World->LineBatcher != NULL && (World->LineBatcher->BatchedLines.Num() || World->LineBatcher->BatchedPoints.Num() || World->LineBatcher->BatchedMeshes.Num() ) )
	{
		World->LineBatcher->Flush();
	}

	if (World->ForegroundLineBatcher != NULL && (World->ForegroundLineBatcher->BatchedLines.Num() || World->ForegroundLineBatcher->BatchedPoints.Num() || World->ForegroundLineBatcher->BatchedMeshes.Num() ) )
	{
		World->ForegroundLineBatcher->Flush();
	}

	// Draw the widget.
	/*if (Widget && bShowWidget)
	{
		Widget->DrawHUD( Canvas );
	}*/

	// Axes indicators
	if (bDrawAxes && !ViewFamily.EngineShowFlags.Game && !GLevelEditorModeTools().IsViewportUIHidden() && !IsVisualizeCalibrationMaterialEnabled())
	{
		// TODO: Figure out how we want the axes widget to be drawn
		DrawAxes(Viewport, Canvas);
	}

	// NOTE: DebugCanvasObject will be created by UDebugDrawService::Draw() if it doesn't already exist.
	FCanvas* DebugCanvas = Viewport->GetDebugCanvas();
	UDebugDrawService::Draw(ViewFamily.EngineShowFlags, Viewport, View, DebugCanvas);
	UCanvas* DebugCanvasObject = FindObjectChecked<UCanvas>(GetTransientPackage(),TEXT("DebugCanvasObject"));
	DebugCanvasObject->Canvas = DebugCanvas;
	DebugCanvasObject->Init( Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y, View , DebugCanvas);

	// Stats display
	if( IsRealtime() && ShouldShowStats() && DebugCanvas)
	{
		const int32 XPos = 4;
		TArray< FDebugDisplayProperty > EmptyPropertyArray;
		DrawStatsHUD( World, Viewport, DebugCanvas, NULL, EmptyPropertyArray, GetViewLocation(), GetViewRotation() );
	}

	if(!IsRealtime())
	{
		// Wait for the rendering thread to finish drawing the view before returning.
		// This reduces the apparent latency of dragging the viewport around.
		FlushRenderingCommands();
	}

	Viewport = ViewportBackup;
}

UE::Widget::EWidgetMode FDisplayClusterLightCardEditorViewportClient::GetWidgetMode() const
{
	return UE::Widget::WM_None;
}

FVector FDisplayClusterLightCardEditorViewportClient::GetWidgetLocation() const
{
	return FEditorViewportClient::GetWidgetLocation();
}

FSceneView* FDisplayClusterLightCardEditorViewportClient::CalcSceneView(FSceneViewFamily* ViewFamily, const int32 StereoViewIndex)
{
	FSceneViewInitOptions ViewInitOptions;
	GetSceneViewInitOptions(ViewInitOptions);

	ViewInitOptions.ViewFamily = ViewFamily;

	TimeForForceRedraw = 0.0;

	FSceneView* View = new FSceneView(ViewInitOptions);

	View->SubduedSelectionOutlineColor = GEngine->GetSubduedSelectionOutlineColor();

	int32 FamilyIndex = ViewFamily->Views.Add(View);
	check(FamilyIndex == View->StereoViewIndex || View->StereoViewIndex == INDEX_NONE);

	View->StartFinalPostprocessSettings(View->ViewLocation);

	OverridePostProcessSettings(*View);

	View->EndFinalPostprocessSettings(ViewInitOptions);

	for (int ViewExt = 0; ViewExt < ViewFamily->ViewExtensions.Num(); ViewExt++)
	{
		ViewFamily->ViewExtensions[ViewExt]->SetupView(*ViewFamily, *View);
	}

	return View;
}

bool FDisplayClusterLightCardEditorViewportClient::InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event,
                                            float AmountDepressed, bool bGamepad)
{
	if ((Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown) && Event == IE_Pressed)
	{
		const int32 Sign = Key == EKeys::MouseScrollUp ? -1 : 1;
		const float CurrentFOV = GetProjectionModeFOV(ProjectionMode);
		const float NewFOV = FMath::Clamp(CurrentFOV + Sign * FOVScrollIncrement, CameraController->GetConfig().MinimumAllowedFOV, CameraController->GetConfig().MaximumAllowedFOV);

		SetProjectionModeFOV(ProjectionMode, NewFOV);
		return true;
	}

	return FEditorViewportClient::InputKey(InViewport, ControllerId, Key, Event, AmountDepressed, bGamepad);
}

bool FDisplayClusterLightCardEditorViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag,
                                                    FRotator& Rot, FVector& Scale)
{
	// TODO: Add logic for moving a light card in "2D" space, modifying its long and lat, and making sure it is flush with the nearest screen

	return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
}

void FDisplayClusterLightCardEditorViewportClient::TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget,
	bool bNudge)
{
	if (!bDraggingActor && bIsDraggingWidget && InInputState.IsLeftMouseButtonPressed() && SelectedLightCards.Num())
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

	if (SelectedLightCards.Num())
	{
		GEditor->DisableDeltaModification(false);
	}
	
	FEditorViewportClient::TrackingStopped();
}

void FDisplayClusterLightCardEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	check(PreviewWorld);

	if (HitProxy)
	{
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			HActor* ActorHitProxy = static_cast<HActor*>(HitProxy);
			if (ActorHitProxy->Actor == RootActorProxy.Get())
			{
				if (ActorHitProxy->PrimComponent && ActorHitProxy->PrimComponent->IsA<UStaticMeshComponent>())
				{
					AActor* TracedLightCard = TraceScreenForLightCard(View, HitX, HitY);
					SelectLightCard(TracedLightCard);
				}
			}
			else if (LightCardProxies.Contains(ActorHitProxy->Actor))
			{
				SelectLightCard(ActorHitProxy->Actor);
			}
			else
			{
				SelectLightCard(nullptr);
			}
		}
	}
	else
	{
		SelectLightCard(nullptr);
	}
	
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

EMouseCursor::Type FDisplayClusterLightCardEditorViewportClient::GetCursor(FViewport* InViewport, int32 X, int32 Y)
{
	EMouseCursor::Type MouseCursor = EMouseCursor::Default;

	if (RequiredCursorVisibiltyAndAppearance.bOverrideAppearance &&
		RequiredCursorVisibiltyAndAppearance.bHardwareCursorVisible)
	{
		 MouseCursor = RequiredCursorVisibiltyAndAppearance.RequiredCursor;
	}
	else if (!RequiredCursorVisibiltyAndAppearance.bHardwareCursorVisible)
	{
		MouseCursor = EMouseCursor::None;
	}
	else if( InViewport->IsCursorVisible() && !bWidgetAxisControlledByDrag )
	{
		HHitProxy* HitProxy = InViewport->GetHitProxy(X,Y);
		if (HitProxy)
		{
			bShouldCheckHitProxy = true;

			if (HitProxy->IsA(HActor::StaticGetType()))
			{
				HActor* ActorHitProxy = static_cast<HActor*>(HitProxy);
				if (ActorHitProxy->Actor == RootActorProxy.Get())
				{
					if (ActorHitProxy->PrimComponent && ActorHitProxy->PrimComponent->IsA<UStaticMeshComponent>())
					{
						FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
							InViewport,
							GetScene(),
							EngineShowFlags)
							.SetRealtimeUpdate(IsRealtime()));
						FSceneView* View = CalcSceneView( &ViewFamily );

						AActor* TracedLightCard = TraceScreenForLightCard(*View, X, Y);
						if (TracedLightCard)
						{
							MouseCursor = EMouseCursor::Crosshairs;
						}
					}
				}
				else if (LightCardProxies.Contains(ActorHitProxy->Actor))
				{
					MouseCursor = EMouseCursor::Crosshairs;
				}
			}
		}
	}

	CachedMouseX = X;
	CachedLastMouseY = Y;

	return MouseCursor;
}

void FDisplayClusterLightCardEditorViewportClient::SelectActor(AActor* NewActor)
{
}

void FDisplayClusterLightCardEditorViewportClient::ResetSelection()
{
	SelectActor(nullptr);
	SetWidgetMode(UE::Widget::WM_None);
}

void FDisplayClusterLightCardEditorViewportClient::UpdatePreviewActor(ADisplayClusterRootActor* RootActor, bool bForce)
{
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	check (PreviewWorld);
	
	if (!bForce && RootActor == RootActorLevelInstance.Get())
	{
		return;
	}

	MeshProjectionRenderer->ClearScene();
	
	if (RootActorProxy.IsValid())
	{
		PreviewWorld->EditorDestroyActor(RootActorProxy.Get(), false);
		RootActorProxy.Reset();
	}
	
	for (const TWeakObjectPtr<AActor>& LightCardProxy : LightCardProxies)
	{
		if (LightCardProxy.IsValid())
		{
			PreviewWorld->EditorDestroyActor(LightCardProxy.Get(), false);
		}
	}

	RootActorLevelInstance.Reset();
	LightCardLevelInstances.Empty();
	LightCardProxies.Empty();
	
	if (RootActor)
	{
		// Schedule for the next tick so CDO changes get propagated first in the event of config editor skeleton
		// regeneration & compiles. nDisplay's custom propagation may have issues if the archetype isn't correct.
		PreviewWorld->GetTimerManager().SetTimerForNextTick([=]()
		{
			RootActorLevelInstance = RootActor;

			{
				FObjectDuplicationParameters DupeActorParameters(RootActor, PreviewWorld->GetCurrentLevel());
				DupeActorParameters.FlagMask = RF_AllFlags & ~(RF_ArchetypeObject | RF_Transactional); // Keeps archetypes correct in config data.
				DupeActorParameters.PortFlags = PPF_DuplicateVerbatim;
			
				RootActorProxy = CastChecked<ADisplayClusterRootActor>(StaticDuplicateObjectEx(DupeActorParameters));
			}
			
			PreviewWorld->GetCurrentLevel()->AddLoadedActor(RootActorProxy.Get());

			// Spawned actor will take the transform values from the template, so manually reset them to zero here
			RootActorProxy->SetActorLocation(FVector::ZeroVector);
			RootActorProxy->SetActorRotation(FRotator::ZeroRotator);

			FindProjectionOriginComponent();

			// Filter out any primitives hidden in game except screen components
			MeshProjectionRenderer->AddActor(RootActorProxy.Get(), [](const UPrimitiveComponent* PrimitiveComponent)
			{
				return !PrimitiveComponent->bHiddenInGame || PrimitiveComponent->IsA<UDisplayClusterScreenComponent>();
			});

			RootActorProxy->UpdatePreviewComponents();

			FindLightCardsForRootActor(RootActor, LightCardLevelInstances);
			
			for (const TWeakObjectPtr<AActor>& LightCard : LightCardLevelInstances)
			{
				FObjectDuplicationParameters DupeActorParameters(LightCard.Get(), PreviewWorld->GetCurrentLevel());
				DupeActorParameters.FlagMask = RF_AllFlags & ~(RF_ArchetypeObject | RF_Transactional);
				DupeActorParameters.PortFlags = PPF_DuplicateVerbatim;
				
				AActor* LightCardProxy = CastChecked<AActor>(StaticDuplicateObjectEx(DupeActorParameters));
				PreviewWorld->GetCurrentLevel()->AddLoadedActor(LightCardProxy);
				
				LightCardProxy->SetActorLocation(LightCard->GetActorLocation() - RootActor->GetActorLocation());
				LightCardProxy->SetActorRotation(LightCard->GetActorRotation() - RootActor->GetActorRotation());

				LightCardProxies.Add(LightCardProxy);
				MeshProjectionRenderer->AddActor(LightCardProxy);
			}

			FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
			FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FDisplayClusterLightCardEditorViewportClient::OnActorPropertyChanged);
		});
	}
}

void FDisplayClusterLightCardEditorViewportClient::SetProjectionMode(EDisplayClusterMeshProjectionType InProjectionMode)
{
	ProjectionMode = InProjectionMode;

	if (ProjectionMode == EDisplayClusterMeshProjectionType::Perspective)
	{
		// TODO: Do we want to cache the perspective rotation and restore it when the user switches back?
		SetViewRotation(FVector::ForwardVector.Rotation());
	}
	else if (ProjectionMode == EDisplayClusterMeshProjectionType::Azimuthal)
	{
		SetViewRotation(FVector::UpVector.Rotation());
	}

	FindProjectionOriginComponent();

	Viewport->InvalidateHitProxy();
	bShouldCheckHitProxy = true;
}

float FDisplayClusterLightCardEditorViewportClient::GetProjectionModeFOV(EDisplayClusterMeshProjectionType InProjectionMode) const
{
	int32 ProjectionModeIndex = (int32)InProjectionMode;
	if (ProjectionFOVs.Num() > ProjectionModeIndex)
	{
		return ProjectionFOVs[ProjectionModeIndex];
	}
	else
	{
		return ViewFOV;
	}
}

void FDisplayClusterLightCardEditorViewportClient::SetProjectionModeFOV(EDisplayClusterMeshProjectionType InProjectionMode, float NewFOV)
{
	int32 ProjectionModeIndex = (int32)InProjectionMode;
	if (ProjectionFOVs.Num() > ProjectionModeIndex)
	{
		ProjectionFOVs[ProjectionModeIndex] = NewFOV;
	}
	else
	{
		ViewFOV = NewFOV;
	}
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

void FDisplayClusterLightCardEditorViewportClient::OnActorPropertyChanged(UObject* ObjectBeingModified,
                                                                          FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	auto IsOurActor = [ObjectBeingModified] (UObject* ObjectToCompare) -> bool
	{
		if (ObjectToCompare)
		{
			if (ObjectBeingModified == ObjectToCompare)
			{
				return true;
			}

			if (const UObject* RootActorOuter = ObjectBeingModified->GetTypedOuter(ObjectToCompare->GetClass()))
			{
				return RootActorOuter == ObjectToCompare;
			}
		}

		return false;
	};
	
	bool bIsOurActor = IsOurActor(RootActorLevelInstance.Get());
	if (!bIsOurActor)
	{
		for (const TWeakObjectPtr<AActor>& LightCard : LightCardLevelInstances)
		{
			bIsOurActor = IsOurActor(LightCard.Get());
			if (bIsOurActor)
			{
				break;
			}
		}
	}
	
	if (bIsOurActor)
	{
		UpdatePreviewActor(RootActorLevelInstance.Get(), true);
	}
}

void FDisplayClusterLightCardEditorViewportClient::GetScenePrimitiveComponents(TArray<UPrimitiveComponent*>& OutPrimitiveComponents)
{
	RootActorProxy->ForEachComponent<UPrimitiveComponent>(true, [&](UPrimitiveComponent* PrimitiveComponent)
	{
		OutPrimitiveComponents.Add(PrimitiveComponent);
	});
}

void FDisplayClusterLightCardEditorViewportClient::GetSceneViewInitOptions(FSceneViewInitOptions& OutViewInitOptions)
{
	FSceneViewInitOptions ViewInitOptions;

	FViewportCameraTransform& ViewTransform = GetViewTransform();

	ViewInitOptions.ViewLocation = ViewTransform.GetLocation();
	ViewInitOptions.ViewRotation = ViewTransform.GetRotation();
	ViewInitOptions.ViewOrigin = ViewInitOptions.ViewLocation;

	FIntPoint ViewportSize = Viewport->GetSizeXY();
	ViewportSize.X = FMath::Max(ViewportSize.X, 1);
	ViewportSize.Y = FMath::Max(ViewportSize.Y, 1);
	FIntPoint ViewportOffset(0, 0);

	ViewInitOptions.SetViewRectangle(FIntRect(ViewportOffset, ViewportOffset + ViewportSize));

	AWorldSettings* WorldSettings = nullptr;
	if (GetScene() != nullptr && GetScene()->GetWorld() != nullptr)
	{
		WorldSettings = GetScene()->GetWorld()->GetWorldSettings();
	}

	if (WorldSettings != nullptr)
	{
		ViewInitOptions.WorldToMetersScale = WorldSettings->WorldToMeters;
	}

	// Rotate view 90 degrees
	ViewInitOptions.ViewRotationMatrix = CalcViewRotationMatrix(ViewInitOptions.ViewRotation) * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	const float MinZ = GetNearClipPlane();
	const float MaxZ = MinZ;
	const float FieldOfView = GetProjectionModeFOV(ProjectionMode);

	// Avoid zero ViewFOV's which cause divide by zero's in projection matrix
	const float MatrixFOV = FMath::Max(0.001f, FieldOfView) * (float)PI / 360.0f;

	float XAxisMultiplier;
	float YAxisMultiplier;

	EAspectRatioAxisConstraint AspectRatioAxisConstraint = GetDefault<ULevelEditorViewportSettings>()->AspectRatioAxisConstraint;

	if (((ViewportSize.X > ViewportSize.Y) && (AspectRatioAxisConstraint == AspectRatio_MajorAxisFOV)) || (AspectRatioAxisConstraint == AspectRatio_MaintainXFOV))
	{
		//if the viewport is wider than it is tall
		XAxisMultiplier = 1.0f;
		YAxisMultiplier = ViewportSize.X / (float)ViewportSize.Y;
	}
	else
	{
		//if the viewport is taller than it is wide
		XAxisMultiplier = ViewportSize.Y / (float)ViewportSize.X;
		YAxisMultiplier = 1.0f;
	}

	if ((bool)ERHIZBuffer::IsInverted)
	{
		ViewInitOptions.ProjectionMatrix = FReversedZPerspectiveMatrix(
			MatrixFOV,
			MatrixFOV,
			XAxisMultiplier,
			YAxisMultiplier,
			MinZ,
			MaxZ
			);
	}
	else
	{
		ViewInitOptions.ProjectionMatrix = FPerspectiveMatrix(
			MatrixFOV,
			MatrixFOV,
			XAxisMultiplier,
			YAxisMultiplier,
			MinZ,
			MaxZ
			);
	}

	if (!ViewInitOptions.IsValidViewRectangle())
	{
		// Zero sized rects are invalid, so fake to 1x1 to avoid asserts later on
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, 1, 1));
	}

	ViewInitOptions.SceneViewStateInterface = ViewState.GetReference();
	ViewInitOptions.ViewElementDrawer = this;

	ViewInitOptions.BackgroundColor = GetBackgroundColor();

	ViewInitOptions.EditorViewBitflag = (uint64)1 << ViewIndex, // send the bit for this view - each actor will check it's visibility bits against this

	// for ortho views to steal perspective view origin
	ViewInitOptions.OverrideLODViewOrigin = FVector::ZeroVector;
	ViewInitOptions.bUseFauxOrthoViewPos = true;

	ViewInitOptions.FOV = FieldOfView;
	ViewInitOptions.OverrideFarClippingPlaneDistance = GetFarClipPlaneOverride();
	ViewInitOptions.CursorPos = CurrentMousePos;

	OutViewInitOptions = ViewInitOptions;
}

UDisplayClusterConfigurationViewport* FDisplayClusterLightCardEditorViewportClient::FindViewportForPrimitiveComponent(UPrimitiveComponent* PrimitiveComponent)
{
	if (RootActorProxy.IsValid())
	{
		const FString PrimitiveComponentName = PrimitiveComponent->GetName();
		UDisplayClusterConfigurationData* Config = RootActorProxy->GetConfigData();
		
		for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& NodePair : Config->Cluster->Nodes)
		{
			const UDisplayClusterConfigurationClusterNode* Node = NodePair.Value;
			for (const TPair<FString, UDisplayClusterConfigurationViewport*>& ViewportPair : Node->Viewports)
			{
				UDisplayClusterConfigurationViewport* CfgViewport = ViewportPair.Value;

				FString ComponentName;
				if (CfgViewport->ProjectionPolicy.Type.Equals(DisplayClusterProjectionStrings::projection::Simple, ESearchCase::IgnoreCase)
					&& CfgViewport->ProjectionPolicy.Parameters.Contains(DisplayClusterProjectionStrings::cfg::simple::Screen))
				{
					ComponentName = CfgViewport->ProjectionPolicy.Parameters[DisplayClusterProjectionStrings::cfg::simple::Screen];
				}
				else if (CfgViewport->ProjectionPolicy.Type.Equals(DisplayClusterProjectionStrings::projection::Mesh, ESearchCase::IgnoreCase)
					&& CfgViewport->ProjectionPolicy.Parameters.Contains(DisplayClusterProjectionStrings::cfg::mesh::Component))
				{
					ComponentName = CfgViewport->ProjectionPolicy.Parameters[DisplayClusterProjectionStrings::cfg::mesh::Component];
				}

				if (ComponentName == PrimitiveComponentName)
				{
					return CfgViewport;
				}
			}
		}
	}

	return nullptr;
}

void FDisplayClusterLightCardEditorViewportClient::FindProjectionOriginComponent()
{
	if (RootActorProxy.IsValid())
	{
		TArray<UDisplayClusterCameraComponent*> ViewOriginComponents;
		RootActorProxy->GetComponents<UDisplayClusterCameraComponent>(ViewOriginComponents);

		if (ViewOriginComponents.Num())
		{
			ProjectionOriginComponent = ViewOriginComponents[0];
		}
		else
		{
			ProjectionOriginComponent = RootActorProxy->GetRootComponent();
		}
	}
	else
	{
		ProjectionOriginComponent.Reset();
	}
}

void FDisplayClusterLightCardEditorViewportClient::FindLightCardsForRootActor(ADisplayClusterRootActor* RootActor, TArray<TWeakObjectPtr<AActor>>& OutLightCards)
{
	if (RootActor)
	{
		FDisplayClusterConfigurationICVFX_VisibilityList& RootActorLightCards = RootActor->GetConfigData()->StageSettings.Lightcard.ShowOnlyList;

		for (const TSoftObjectPtr<AActor>& LightCardActor : RootActorLightCards.Actors)
		{
			if (LightCardActor.IsValid())
			{
				OutLightCards.Add(LightCardActor.Get());
			}
		}

		// If there are any layers that are specified as light card layers, iterate over all actors in the world and 
		// add any that are members of any of the light card layers to the list. Only add an actor once, even if it is
		// in multiple layers
		if (RootActorLightCards.ActorLayers.Num())
		{
			if (UWorld* World = RootActor->GetWorld())
			{
				for (const TWeakObjectPtr<AActor> WeakActor : FActorRange(World))
				{
					if (WeakActor.IsValid())
					{
						for (const FActorLayer& ActorLayer : RootActorLightCards.ActorLayers)
						{
							if (WeakActor->Layers.Contains(ActorLayer.Name))
							{
								OutLightCards.Add(WeakActor);
								break;
							}
						}
					}
				}
			}
		}
	}
}

bool FDisplayClusterLightCardEditorViewportClient::IsLightCardSelected(const AActor* Actor)
{
	return SelectedLightCards.Contains(Actor);
}

void FDisplayClusterLightCardEditorViewportClient::SelectLightCard(AActor* Actor, bool bAddToSelection)
{
	TArray<AActor*> UpdatedActors;

	if (!bAddToSelection)
	{
		for (const TWeakObjectPtr<AActor>& LightCard : SelectedLightCards)
		{
			if (LightCard.IsValid())
			{
				UpdatedActors.Add(LightCard.Get());
			}
		}

		SelectedLightCards.Empty();
	}

	if (Actor)
	{
		SelectedLightCards.Add(Actor);
		UpdatedActors.Add(Actor);
	}

	for (AActor* UpdatedActor : UpdatedActors)
	{
		UpdatedActor->PushSelectionToProxies();
	}
}

AActor* FDisplayClusterLightCardEditorViewportClient::TraceScreenForLightCard(const FSceneView& View, int32 HitX, int32 HitY)
{
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	check(PreviewWorld);

	const FVector4 ScreenPos = View.PixelToScreen(HitX, HitY, 0);
	const FMatrix InvProjMatrix = View.ViewMatrices.GetInvProjectionMatrix();
	const FMatrix InvViewMatrix = View.ViewMatrices.GetInvViewMatrix();
	FVector ViewPos = FVector(InvProjMatrix.TransformFVector4(FVector4(ScreenPos.X * GNearClippingPlane, ScreenPos.Y * GNearClippingPlane, 0.0f, GNearClippingPlane)));
	
	// If the projection mode is azimuthal, we need to convert to the "true" view position by undoing the non-linear projection applied by the projection shader
	if (ProjectionMode == EDisplayClusterMeshProjectionType::Azimuthal)
	{
		const float Rho = ViewPos.Length();
		const FVector UnitViewPos = ViewPos.GetSafeNormal();

		const FVector PlanePos = UnitViewPos / UnitViewPos.Z;
		const FVector2D PolarCoords = FVector2D(FMath::Sqrt(PlanePos.X * PlanePos.X + PlanePos.Y * PlanePos.Y), FMath::Atan2(PlanePos.Y, PlanePos.X));
		ViewPos = FVector(FMath::Sin(PolarCoords.X) * FMath::Cos(PolarCoords.Y), FMath::Sin(PolarCoords.X) * FMath::Sin(PolarCoords.Y), FMath::Cos(PolarCoords.X)) * Rho;
	}

	const FVector Direction = InvViewMatrix.TransformVector(ViewPos).GetSafeNormal();

	const FVector CursorRayStart = View.ViewMatrices.GetViewOrigin();
	const FVector CursorRayEnd = CursorRayStart + Direction * HALF_WORLD_MAX;

	FCollisionQueryParams Param(SCENE_QUERY_STAT(DragDropTrace), true);

	bool bHitLightCard = false;
	FHitResult ScreenHitResult;
	if (PreviewWorld->LineTraceSingleByObjectType(ScreenHitResult, CursorRayStart, CursorRayEnd, FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllObjects), Param))
	{
		if (AActor* HitActor = ScreenHitResult.GetActor())
		{
			if (RootActorProxy.Get() == HitActor && ScreenHitResult.Component.IsValid())
			{
				if (UDisplayClusterConfigurationViewport* CfgViewport = FindViewportForPrimitiveComponent(ScreenHitResult.Component.Get()))
				{
					FString ViewOriginName = CfgViewport->Camera;
					if (UDisplayClusterCameraComponent* ViewOrigin = RootActorProxy->GetComponentByName<UDisplayClusterCameraComponent>(ViewOriginName))
					{
						const FVector ViewOriginRayStart = ViewOrigin->GetComponentLocation();
						const FVector ViewOriginRayEnd = ViewOriginRayStart + (ScreenHitResult.Location - ViewOriginRayStart) * HALF_WORLD_MAX;

						TArray<FHitResult> HitResults;
						if (PreviewWorld->LineTraceMultiByObjectType(HitResults, ViewOriginRayStart, ViewOriginRayEnd, FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllObjects), Param))
						{
							for (FHitResult& HitResult : HitResults)
							{
								if (LightCardProxies.Contains(HitResult.GetActor()))
								{
									return HitResult.GetActor();
								}
							}
						}
					}
				}
			}
			else if (LightCardProxies.Contains(HitActor))
			{
				return HitActor;
			}
		}
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
