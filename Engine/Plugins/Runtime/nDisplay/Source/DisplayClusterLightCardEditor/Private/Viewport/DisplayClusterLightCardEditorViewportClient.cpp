// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorViewportClient.h"

#include "DisplayClusterConfigurationTypes.h"
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

	if (RootActorPreviewInstance.IsValid() && RootActorLevelInstance.IsValid())
	{
		// Pass the preview render targets from the level instance root actor to the preview root actor
		UDisplayClusterConfigurationData* Config = RootActorLevelInstance->GetConfigData();

		for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& NodePair : Config->Cluster->Nodes)
		{
			const UDisplayClusterConfigurationClusterNode* Node = NodePair.Value;
			for (const TPair<FString, UDisplayClusterConfigurationViewport*>& ViewportPair : Node->Viewports)
			{
				UDisplayClusterPreviewComponent* LevelInstancePreviewComp = RootActorLevelInstance->GetPreviewComponent(NodePair.Key, ViewportPair.Key);
				UDisplayClusterPreviewComponent* PreviewComp = RootActorPreviewInstance->GetPreviewComponent(NodePair.Key, ViewportPair.Key);

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

	MeshProjectionRenderer->Render(Canvas, GetScene(), ViewInitOptions, ProjectionMode);

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
	// TODO: Add logic to check if the hit proxy was a light card, and select that light card
	
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

void FDisplayClusterLightCardEditorViewportClient::SelectActor(AActor* NewActor)
{
}

void FDisplayClusterLightCardEditorViewportClient::ResetSelection()
{
	SelectActor(nullptr);
	SetWidgetMode(UE::Widget::WM_None);
}

void FDisplayClusterLightCardEditorViewportClient::UpdatePreviewActor(ADisplayClusterRootActor* RootActor)
{
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	check (PreviewWorld);
	
	if (RootActorPreviewInstance.IsValid() && (!RootActor || !PreviewWorld->ContainsActor(RootActor)))
	{
		PreviewWorld->EditorDestroyActor(RootActorPreviewInstance.Get(), false);
		
		RootActorPreviewInstance.Reset();
		RootActorLevelInstance.Reset();

		MeshProjectionRenderer->ClearScene();
	}
	
	if (RootActor)
	{
		RootActorLevelInstance = RootActor;

		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.bNoFail = true;
		SpawnInfo.ObjectFlags = RF_Transient|RF_Transactional;
		SpawnInfo.Template = RootActor;
		
		RootActorPreviewInstance = CastChecked<ADisplayClusterRootActor>(PreviewWorld->SpawnActor(RootActor->GetClass(),
			nullptr,  nullptr, MoveTemp(SpawnInfo)));
		
		PreviewWorld->GetCurrentLevel()->AddLoadedActor(RootActorPreviewInstance.Get());

		// Copy component data and update transforms.
		{
			TArray<UActorComponent*> SpawnedActorComponents;
			TArray<UActorComponent*> LevelActorComponents;
			RootActorPreviewInstance->GetComponents(SpawnedActorComponents);
			RootActorLevelInstance->GetComponents(LevelActorComponents);

			for (UActorComponent* SpawnedComponent : SpawnedActorComponents)
			{
				if (UActorComponent** OriginalComponent = LevelActorComponents.FindByPredicate([SpawnedComponent](const UActorComponent* Component)
				{
					return Component->GetFName() == SpawnedComponent->GetFName();
				}))
				{
					UEngine::CopyPropertiesForUnrelatedObjects(*OriginalComponent, SpawnedComponent);
					SpawnedComponent->UpdateComponentToWorld();
				}
			}
		}

		// Spawned actor will take the transform values from the template, so manually reset them to zero here
		RootActorPreviewInstance->SetActorLocation(FVector::ZeroVector);
		RootActorPreviewInstance->SetActorRotation(FRotator::ZeroRotator);

		FindProjectionOriginComponent();

		// Filter out any primitives hidden in game except screen components
		MeshProjectionRenderer->AddActor(RootActorPreviewInstance.Get(), [](const UPrimitiveComponent* PrimitiveComponent)
		{
			return !PrimitiveComponent->bHiddenInGame || PrimitiveComponent->IsA<UDisplayClusterScreenComponent>();
		});

		RootActorPreviewInstance->UpdatePreviewComponents();

		FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FDisplayClusterLightCardEditorViewportClient::OnActorPropertyChanged);
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
	
	bool bIsOurActor = ObjectBeingModified == RootActorLevelInstance;
	if (!bIsOurActor)
	{
		if (const UObject* RootActorOuter = ObjectBeingModified->GetTypedOuter(ADisplayClusterRootActor::StaticClass()))
		{
			bIsOurActor = RootActorOuter == RootActorLevelInstance;
		}
	}
	if (bIsOurActor)
	{
		UpdatePreviewActor(RootActorLevelInstance.Get());
	}
}

void FDisplayClusterLightCardEditorViewportClient::GetScenePrimitiveComponents(TArray<UPrimitiveComponent*>& OutPrimitiveComponents)
{
	RootActorPreviewInstance->ForEachComponent<UPrimitiveComponent>(true, [&](UPrimitiveComponent* PrimitiveComponent)
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

void FDisplayClusterLightCardEditorViewportClient::FindProjectionOriginComponent()
{
	if (RootActorPreviewInstance.IsValid())
	{
		if (ProjectionMode == EDisplayClusterMeshProjectionType::Perspective)
		{
			TArray<UDisplayClusterCameraComponent*> ViewOriginComponents;
			RootActorPreviewInstance->GetComponents<UDisplayClusterCameraComponent>(ViewOriginComponents);

			if (ViewOriginComponents.Num())
			{
				ProjectionOriginComponent = ViewOriginComponents[0];
			}
			else
			{
				ProjectionOriginComponent = RootActorPreviewInstance->GetRootComponent();
			}
		}
		else if (ProjectionMode == EDisplayClusterMeshProjectionType::Azimuthal)
		{
			ProjectionOriginComponent = RootActorPreviewInstance->GetRootComponent();
		}
	}
	else
	{
		ProjectionOriginComponent.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
