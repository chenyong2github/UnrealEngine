// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorViewportClient.h"

#include "DisplayClusterLightcardEditorViewport.h"
#include "DisplayClusterLightCardEditorWidget.h"
#include "LightCardTemplates/DisplayClusterLightCardTemplate.h"

#include "Blueprints/DisplayClusterBlueprintLib.h"
#include "Components/DisplayClusterPreviewComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterLightCardEditorHelper.h"
#include "DisplayClusterProjectionStrings.h"
#include "DisplayClusterRootActor.h"
#include "DisplayClusterLightCardActor.h"
#include "DisplayClusterLightCardEditorLog.h"
#include "IDisplayClusterScenePreview.h"
#include "SDisplayClusterLightCardEditor.h"

#include "AudioDevice.h"
#include "CameraController.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CustomEditorStaticScreenPercentage.h"
#include "Debug/DebugDrawService.h"
#include "EngineUtils.h"
#include "EditorModes.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorModeManager.h"
#include "EngineModule.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "KismetProceduralMeshLibrary.h"
#include "LegacyScreenPercentageDriver.h"
#include "Math/UnrealMathUtility.h"
#include "PreviewScene.h"
#include "ProceduralMeshComponent.h"
#include "RayTracingDebugVisualizationMenuCommands.h"
#include "Renderer/Private/SceneRendering.h"
#include "ScopedTransaction.h"
#include "Slate/SceneViewport.h"
#include "UnrealEdGlobals.h"
#include "UnrealWidget.h"
#include "Widgets/Docking/SDockTab.h"

#if WITH_OPENCV

#include "OpenCVHelper.h"

#include "PreOpenCVHeaders.h"
#include "opencv2/imgproc.hpp"
#include "PostOpenCVHeaders.h"

#endif //WITH_OPENCV

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditorViewportClient"

//////////////////////////////////////////////////////////////////////////
// FDisplayClusterLightCardEditorViewportClient

FDisplayClusterLightCardEditorViewportClient::FDisplayClusterLightCardEditorViewportClient(FPreviewScene& InPreviewScene,
	const TWeakPtr<SDisplayClusterLightCardEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(nullptr, &InPreviewScene, InEditorViewportWidget)
	, LightCardEditorViewportPtr(InEditorViewportWidget)
{
	check(InEditorViewportWidget.IsValid());
	
	LightCardEditorPtr = InEditorViewportWidget.Pin()->GetLightCardEditor();
	check(LightCardEditorPtr.IsValid())

	IDisplayClusterScenePreview& PreviewModule = IDisplayClusterScenePreview::Get();
	PreviewRendererId = PreviewModule.CreateRenderer();
	PreviewModule.SetRendererActorSelectedDelegate(
		PreviewRendererId,
		FDisplayClusterMeshProjectionRenderer::FSelection::CreateRaw(this, &FDisplayClusterLightCardEditorViewportClient::IsLightCardSelected)
	);
	PreviewModule.SetRendererRenderSimpleElementsDelegate(
		PreviewRendererId,
		FDisplayClusterMeshProjectionRenderer::FSimpleElementPass::CreateRaw(this, &FDisplayClusterLightCardEditorViewportClient::Draw)
	);

	ProjectionHelper = MakeShared<FDisplayClusterLightCardEditorHelper>(PreviewRendererId);

	InputMode = EInputMode::Idle;
	DragWidgetOffset = FVector::ZeroVector;
	
	EditorWidget = MakeShared<FDisplayClusterLightCardEditorWidget>();

	// Setup defaults for the common draw helper.
	bUsesDrawHelper = false;

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

	ResetFOVs();

	//This seems to be needed to get the correct world time in the preview.
	SetIsSimulateInEditorViewport(true);
	
	UpdatePreviewActor(LightCardEditorPtr.Pin()->GetActiveRootActor().Get());

	SetProjectionMode(EDisplayClusterMeshProjectionType::Azimuthal, ELevelViewportType::LVT_Perspective);
}

FDisplayClusterLightCardEditorViewportClient::~FDisplayClusterLightCardEditorViewportClient()
{
	IDisplayClusterScenePreview::Get().DestroyRenderer(PreviewRendererId);

	EndTransaction();
	if (RootActorLevelInstance.IsValid())
	{
		RootActorLevelInstance->UnsubscribeFromPostProcessRenderTarget(reinterpret_cast<uint8*>(this));
	}

	if (RefreshTimerHandle.IsValid())
	{
		if (const UWorld* PreviewWorld = PreviewScene->GetWorld())
		{
			PreviewWorld->GetTimerManager().ClearTimer(RefreshTimerHandle);
		}
	}
}

FLinearColor FDisplayClusterLightCardEditorViewportClient::GetBackgroundColor() const
{
	return FLinearColor::Gray;
}

void FDisplayClusterLightCardEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// Camera position is locked to a specific location
	ResetCamera(/* bLocationOnly */ true);

	CalcEditorWidgetTransform(CachedEditorWidgetWorldTransform);

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
					PreviewComp->SetOverrideTexture(LevelInstancePreviewComp->GetRenderTargetTexturePostProcess());
				}
			}
		}
	}

	// EditorViewportClient sets the cursor settings based on the state of the built in FWidget, which isn't being used here, so
	// force a hardware cursor if we are dragging an actor so that the correct mouse cursor shows up
	switch (InputMode)
	{
	case EInputMode::DraggingActor:
		SetRequiredCursor(true, false);
		SetRequiredCursorOverride(true, EMouseCursor::GrabHandClosed);
		break;

	case EInputMode::DrawingLightCard:
		SetRequiredCursor(true, false);
		SetRequiredCursorOverride(true, EMouseCursor::Crosshairs);
		break;
	}

	if (DesiredLookAtLocation.IsSet())
	{
		const FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(GetViewLocation(), *DesiredLookAtLocation);
		const FRotator NewRotation = FMath::RInterpTo(GetViewRotation(), LookAtRotation, DeltaSeconds, DesiredLookAtSpeed);
		SetViewRotation(NewRotation);

		FSceneViewInitOptions SceneVewInitOptions;
		GetSceneViewInitOptions(SceneVewInitOptions);
		FViewMatrices ViewMatrices(SceneVewInitOptions);

		FVector ProjectedEditorWidgetPosition = ProjectWorldPosition(CachedEditorWidgetWorldTransform.GetTranslation(), ViewMatrices);

		if (NewRotation.Equals(LookAtRotation, 2.f) ||
			!IsLocationCloseToEdge(ProjectedEditorWidgetPosition))
		{
			DesiredLookAtLocation.Reset();
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

	if (!bDisableCustomRenderer)
	{
		FDisplayClusterMeshProjectionRenderSettings RenderSettings;
		RenderSettings.RenderType = EDisplayClusterMeshProjectionOutput::Color;
		RenderSettings.EngineShowFlags = EngineShowFlags;
		RenderSettings.ProjectionType = ProjectionMode;
		RenderSettings.PrimitiveFilter.ShouldRenderPrimitiveDelegate = FDisplayClusterMeshProjectionPrimitiveFilter::FPrimitiveFilter::CreateSP(this, &FDisplayClusterLightCardEditorViewportClient::ShouldRenderPrimitive);
		RenderSettings.PrimitiveFilter.ShouldApplyProjectionDelegate = FDisplayClusterMeshProjectionPrimitiveFilter::FPrimitiveFilter::CreateSP(this, &FDisplayClusterLightCardEditorViewportClient::ShouldApplyProjectionToPrimitive);

		if (ProjectionMode == EDisplayClusterMeshProjectionType::UV)
		{
			RenderSettings.ProjectionTypeSettings.UVProjectionIndex = 1;
			RenderSettings.ProjectionTypeSettings.UVProjectionPlaneSize = ADisplayClusterLightCardActor::UVPlaneDefaultSize;
			RenderSettings.ProjectionTypeSettings.UVProjectionPlaneDistance = ADisplayClusterLightCardActor::UVPlaneDefaultDistance;
		}

		GetSceneViewInitOptions(RenderSettings.ViewInitOptions);

		IDisplayClusterScenePreview::Get().Render(PreviewRendererId, RenderSettings, *Canvas);
	}
	else
	{
		GetRendererModule().BeginRenderingViewFamily(Canvas, &ViewFamily);
	}

	if (View)
	{
		DrawCanvas(*Viewport, *View, *Canvas);
	}

	if (bDisplayNormalMapVisualization)
	{
		auto DrawNormalMap = [Canvas, this](bool bShowNorthMap, FVector2D Position)
		{
			if (const UTexture* NormalMapTexture = ProjectionHelper->GetNormalMapTexture(bShowNorthMap))
			{
				Canvas->DrawTile(Position.X, Position.Y, 512, 512, 0, 0, 1, 1, FLinearColor::White, NormalMapTexture->GetResource());
			}
		};

		DrawNormalMap(true, FVector2D(0.0f, 0.0f));
		DrawNormalMap(false, FVector2D(0.0f, 512.0f));
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
		// Don't draw the usual 3D axes if the projection mode is UV
		if (ProjectionMode != EDisplayClusterMeshProjectionType::UV)
		{
			DrawAxes(Viewport, Canvas);
		}
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

void FDisplayClusterLightCardEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const bool bIsUVProjection = ProjectionMode == EDisplayClusterMeshProjectionType::UV;
	if (LastSelectedLightCard.IsValid() && LastSelectedLightCard->bIsUVLightCard == bIsUVProjection)
	{
		// Project the editor widget's world position into projection space so that it renders at the appropriate screen location.
		// This needs to be computed on the render thread using the render thread's scene view, which will be behind the game thread's scene view
		// by at least one frame

		EditorWidget->SetTransform(CachedEditorWidgetWorldTransform);
		EditorWidget->SetProjectionTransform(FDisplayClusterMeshProjectionTransform(ProjectionMode, View->ViewMatrices.GetViewMatrix()));
		EditorWidget->Draw(View, this, PDI);
	}
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

bool FDisplayClusterLightCardEditorViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	switch (InputMode)
	{
	case EInputMode::DrawingLightCard:
		
		if ((EventArgs.Key == EKeys::LeftMouseButton) && (EventArgs.Event == IE_Released))
		{
			// Add a new light card polygon point

			FIntPoint MousePos;
			EventArgs.Viewport->GetMousePos(MousePos);

			if (!DrawnMousePositions.Num() || (DrawnMousePositions.Last() != MousePos))
			{
				DrawnMousePositions.Add(MousePos);
			}
		}
		else if ((EventArgs.Key == EKeys::RightMouseButton) && (EventArgs.Event == IE_Released))
		{
			// Create the polygon light card.
			CreateDrawnLightCard(DrawnMousePositions);

			// Reset the list of polygon points used to create the new card
			DrawnMousePositions.Empty();

			// We go back to idle input mode
			InputMode = EInputMode::Idle;
		}

		// Returning here (and not calling Super::InputKey) locks the viewport so that the user does not inadvertently 
		// change the perspective while in the middle of drawing a light card, as that is not currently supported
		return true;

	default:

		if ((EventArgs.Key == EKeys::MouseScrollUp || EventArgs.Key == EKeys::MouseScrollDown) && (EventArgs.Event == IE_Pressed))
		{
			const int32 Sign = EventArgs.Key == EKeys::MouseScrollUp ? -1 : 1;
			const float CurrentFOV = GetProjectionModeFOV(ProjectionMode);
			const float NewFOV = FMath::Clamp(CurrentFOV + Sign * FOVScrollIncrement, CameraController->GetConfig().MinimumAllowedFOV, CameraController->GetConfig().MaximumAllowedFOV);

			SetProjectionModeFOV(ProjectionMode, NewFOV);
			return true;
		}

		break;
	}

	return FEditorViewportClient::InputKey(EventArgs);
}

bool FDisplayClusterLightCardEditorViewportClient::InputWidgetDelta(FViewport* InViewport,
	EAxisList::Type CurrentAxis,
	FVector& Drag,
	FRotator& Rot,
	FVector& Scale)
{
	bool bHandled = false;

	if (FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale))
	{
		bHandled = true;
	}
	else
	{
		if (CurrentAxis != EAxisList::Type::None && SelectedLightCards.Num())
		{
			switch (EditorWidget->GetWidgetMode())
			{
			case FDisplayClusterLightCardEditorWidget::EWidgetMode::WM_Translate:
				if (ProjectionMode == EDisplayClusterMeshProjectionType::UV)
				{
					MoveSelectedUVLightCards(InViewport, CurrentAxis);
				}
				else
				{
					MoveSelectedLightCards(InViewport, CurrentAxis);
				}
				break;

			case FDisplayClusterLightCardEditorWidget::EWidgetMode::WM_RotateZ:
				SpinSelectedLightCards(InViewport);
				break;

			case FDisplayClusterLightCardEditorWidget::EWidgetMode::WM_Scale:
				ScaleSelectedLightCards(InViewport, CurrentAxis);
				break;
			}

			bHandled = true;
		}

		InViewport->GetMousePos(LastWidgetMousePos);
	}

	return bHandled;
}

void FDisplayClusterLightCardEditorViewportClient::TrackingStarted(
	const FInputEventState& InInputState, 
	bool bIsDraggingWidget,
	bool bNudge)
{
	if ((InputMode == EInputMode::Idle) && bIsDraggingWidget && InInputState.IsLeftMouseButtonPressed() && SelectedLightCards.Num())
	{
		// Start dragging actor
		InputMode = EInputMode::DraggingActor;

		GEditor->DisableDeltaModification(true);
		{
			// The pivot location won't update properly and the actor will rotate / move around the original selection origin
			// so update it here to fix that.
			GUnrealEd->UpdatePivotLocationForSelection();
			GUnrealEd->SetPivotMovedIndependently(false);
		}

		BeginTransaction(LOCTEXT("MoveLightCard", "Move Light Card"));

		DesiredLookAtLocation.Reset();

		// Compute and store the delta between the widget's origin and the place the user clicked on it,
		// in order to factor it out when transforming the selected actor
		FIntPoint MousePos;
		InInputState.GetViewport()->GetMousePos(MousePos);

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			InInputState.GetViewport(),
			GetScene(),
			EngineShowFlags)
			.SetRealtimeUpdate(IsRealtime()));

		FSceneView* View = CalcSceneView(&ViewFamily);

		FVector Origin;
		FVector Direction;
		ProjectionHelper->PixelToWorld(*View, MousePos, Origin, Direction);

		if (RenderViewportType != LVT_Perspective)
		{
			// For orthogonal projections, the drag widget offset should store the origin offset instead of the direction offset,
			// since the direction offset is always the same regardless of which pixel has been clicked while the origin moves
			DragWidgetOffset = Origin - FPlane::PointPlaneProject(CachedEditorWidgetWorldTransform.GetTranslation(), FPlane(Origin, Direction));
		}
		else
		{
			DragWidgetOffset = Direction - (CachedEditorWidgetWorldTransform.GetTranslation() - Origin).GetSafeNormal();
		}

		LastWidgetMousePos = MousePos;
	}

	FEditorViewportClient::TrackingStarted(InInputState, bIsDraggingWidget, bNudge);
}

void FDisplayClusterLightCardEditorViewportClient::TrackingStopped()
{
	if (InputMode == EInputMode::DraggingActor)
	{
		InputMode = EInputMode::Idle;

		DragWidgetOffset = FVector::ZeroVector;
		EndTransaction();

		if (SelectedLightCards.Num())
		{
			GEditor->DisableDeltaModification(false);
		}
	}

	FEditorViewportClient::TrackingStopped();
}

void FDisplayClusterLightCardEditorViewportClient::CreateDrawnLightCard(const TArray<FIntPoint>& MousePositions)
{
#if WITH_OPENCV
	if (DrawnMousePositions.Num() < 3 || !Viewport)
	{
		return;
	}

	//
	// Find direction of each mouse position
	//

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		GetScene(),
		EngineShowFlags
	).SetRealtimeUpdate(IsRealtime()));

	FSceneView* View = CalcSceneView(&ViewFamily);
	const FVector ViewOrigin = View->ViewLocation;

	TArray<FVector> MouseWorldDirections; // directions from view origin

	FVector ScreenOrigin;
	FVector ScreenDirection;
	MouseWorldDirections.AddUninitialized(MousePositions.Num());

	for (int32 PointIdx = 0; PointIdx < MousePositions.Num(); ++PointIdx)
	{
		ProjectionHelper->CalculateOriginAndDirectionFromPixelPosition(MousePositions[PointIdx], *View, FVector::ZeroVector, ScreenOrigin, ScreenDirection);
		MouseWorldDirections[PointIdx] = ScreenDirection;
	}

	//
	// Find light card direction based on center of bounds of world positions of mouse positions
	//

	FVector LightCardDirection; // direction from origin to where pixel points to

	{
		double MinX =  DBL_MAX;
		double MinY =  DBL_MAX;
		double MinZ =  DBL_MAX;

		double MaxX = -DBL_MAX;
		double MaxY = -DBL_MAX;
		double MaxZ = -DBL_MAX;

		for (const FVector& MouseWorldDirection : MouseWorldDirections)
		{
			FVector MouseWorldPosition;
			FVector MouseRelativeNormal;

			ProjectionHelper->CalculateNormalAndPositionInDirection(ViewOrigin, MouseWorldDirection, MouseWorldPosition, MouseRelativeNormal);

			MinX = FMath::Min(MinX, MouseWorldPosition.X);
			MinY = FMath::Min(MinY, MouseWorldPosition.Y);
			MinZ = FMath::Min(MinZ, MouseWorldPosition.Z);

			MaxX = FMath::Max(MaxX, MouseWorldPosition.X);
			MaxY = FMath::Max(MaxY, MouseWorldPosition.Y);
			MaxZ = FMath::Max(MaxZ, MouseWorldPosition.Z);
		}

		const FVector MouseWorldBoundsCenter(
			(MaxX + MinX) / 2,
			(MaxY + MinY) / 2,
			(MaxZ + MinZ) / 2
		);

		LightCardDirection = (MouseWorldBoundsCenter - ViewOrigin).GetSafeNormal();

		if (LightCardDirection == FVector::ZeroVector)
		{
			UE_LOG(DisplayClusterLightCardEditorLog, Warning, TEXT("Could not determine a LightCardDirection for the given polygon"));
			return;
		}
	}

	//
	// Find world position and normal of card, and its plane axes
	// 

	FVector LightCardLocation; // Wolrd location of light card
	FVector LightCardPlaneAxisX; // Normal vector at the found enveloping surface point
	FVector LightCardPlaneAxisY;
	FVector LightCardPlaneAxisZ;

	{
		FVector LightCardRelativeNormal;

		ProjectionHelper->CalculateNormalAndPositionInDirection(ViewOrigin, LightCardDirection, LightCardLocation, LightCardRelativeNormal);

		const FMatrix RotEffector = FRotationMatrix::MakeFromX(-LightCardRelativeNormal);
		const FMatrix RotArm = ProjectionMode != EDisplayClusterMeshProjectionType::UV ? FRotationMatrix::MakeFromX(LightCardDirection) : FMatrix::Identity;
		const FRotator TotalRotator = (RotEffector * RotArm).Rotator();

		LightCardPlaneAxisX = TotalRotator.RotateVector(-FVector::XAxisVector); // Same as Normal
		LightCardPlaneAxisY = TotalRotator.RotateVector(-FVector::YAxisVector);
		LightCardPlaneAxisZ = TotalRotator.RotateVector( FVector::ZAxisVector);
	}

	//
	// Project mouse positions to light card plane.
	//

	TArray<FVector3d> ProjectedMousePositions;
	ProjectedMousePositions.Reserve(MouseWorldDirections.Num());
	const FPlane LightCardPlane = FPlane(LightCardLocation, LightCardPlaneAxisX);

	for (int32 PointIdx = 0; PointIdx < MouseWorldDirections.Num(); ++PointIdx)
	{
		// skip the point if the intersection doesn't exist, which happens when the normal and the 
		// ray direction are perpendicular (nearly zero dot product)
		if (FMath::IsNearlyZero(FVector::DotProduct(LightCardPlaneAxisX, MouseWorldDirections[PointIdx])))
		{
			continue;
		}

		ProjectedMousePositions.Add(FMath::RayPlaneIntersection(ViewOrigin, MouseWorldDirections[PointIdx], LightCardPlane));
	}

	// Convert projected mouse positions in world space to light card plane space (2d)

	TArray<FVector2d> PointsInLightCardPlane;
	PointsInLightCardPlane.Reserve(ProjectedMousePositions.Num());

	for (const FVector& ProjectedMousePosition: ProjectedMousePositions)
	{
		const FVector ProjMousePosLocal = ProjectedMousePosition - LightCardLocation; 

		PointsInLightCardPlane.Add(FVector2D(
			-FVector::DotProduct(ProjMousePosLocal, LightCardPlaneAxisY),
			 FVector::DotProduct(ProjMousePosLocal, LightCardPlaneAxisZ))
		);
	}

	// Find the spin that minimizes the area. 
	// For now, simply a fixed size linear search but a better optimization algorithm could be used.

	double Spin = 0;
	double LightCardWidth = 0;
	double LightCardHeight = 0;

	{
		double Area = DBL_MAX;
		double constexpr SpinStepSize = 15;

		for (double SpinTest = 0; SpinTest < 180; SpinTest += SpinStepSize)
		{
			double LightCardWidthTest = 0;
			double LightCardHeightTest = 0;

			const FRotator Rotator(0, -SpinTest, 0); // using yaw for spin for convenience

			for (const FVector2d& Point : PointsInLightCardPlane)
			{
				FVector RotatedPoint = Rotator.RotateVector(FVector(Point.X, Point.Y, 0));

				LightCardWidthTest = FMath::Max(LightCardWidthTest, 2 * abs(RotatedPoint.X));
				LightCardHeightTest = FMath::Max(LightCardHeightTest, 2 * abs(RotatedPoint.Y));
			}

			const double AreaTest = LightCardWidthTest * LightCardHeightTest;

			if (AreaTest < Area)
			{
				Spin = SpinTest;
				Area = AreaTest;
				LightCardWidth = LightCardWidthTest;
				LightCardHeight = LightCardHeightTest;
			}
		}
	}

	// Update the points with the rotated ones, since we're going to spin the card.
	{
		const FRotator Rotator(0, -Spin, 0);

		for (FVector2d& Point : PointsInLightCardPlane)
		{
			FVector RotatedPoint = Rotator.RotateVector(FVector(Point.X, Point.Y, 0));

			Point.X = RotatedPoint.X;
			Point.Y = RotatedPoint.Y;
		}
	}

	if (FMath::IsNearlyZero(LightCardWidth) || FMath::IsNearlyZero(LightCardHeight))
	{
		UE_LOG(DisplayClusterLightCardEditorLog, Warning, TEXT("Could not create new light card because one or more of its dimensions was zero."));
		return;
	}

	// Create lightcard

	if (!LightCardEditorPtr.IsValid())
	{
		UE_LOG(DisplayClusterLightCardEditorLog, Warning, TEXT("Could not create new light card because LightCardEditorPtr was not valid."));
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("AddNewLightCard", "Add New Light Card"));

	ADisplayClusterLightCardActor* LightCard = LightCardEditorPtr.Pin()->SpawnLightCard();

	if (!LightCard)
	{
		UE_LOG(DisplayClusterLightCardEditorLog, Warning, TEXT("Could not create new light card because AddNewLightCard failed."));
		return;
	}

	LightCard->Spin = Spin;

	// Assign polygon mask

	LightCard->Mask = EDisplayClusterLightCardMask::Polygon;

	LightCard->Polygon.Empty(PointsInLightCardPlane.Num());

	for (const FVector2d& PlanePoint : PointsInLightCardPlane)
	{
		LightCard->Polygon.Add(FVector2D(
			PlanePoint.X / LightCardWidth  + 0.5,
			PlanePoint.Y / LightCardHeight + 0.5
		));
	}

	LightCard->UpdatePolygonTexture();
	LightCard->UpdateLightCardMaterialInstance();

	// Update scale to match the desired size
	{
		// The default card plane is a 100x100 square.
		constexpr double LightCardPlaneWidth = 100;
		constexpr double LightCardPlaneHeight = 100;

		LightCard->Scale.X = LightCardWidth / LightCardPlaneWidth;
		LightCard->Scale.Y = LightCardHeight / LightCardPlaneHeight;
	}

	// Update position
	if (ProjectionMode == EDisplayClusterMeshProjectionType::UV)
	{
		const float UVProjectionPlaneSize = ADisplayClusterLightCardActor::UVPlaneDefaultSize;

		const FVector DesiredLocation = LightCardLocation - ViewOrigin;
		const FVector2D DesiredUVLocation = FVector2D(DesiredLocation.Y / UVProjectionPlaneSize + 0.5f, 0.5f - DesiredLocation.Z / UVProjectionPlaneSize);

		LightCard->UVCoordinates = DesiredUVLocation;
	}
	else
	{
		const FDisplayClusterLightCardEditorHelper::FSphericalCoordinates LightCardCoords(LightCardLocation - ViewOrigin);
		MoveLightCardTo(*LightCard, LightCardLocation - ViewOrigin);
	}

#endif // WITH_OPENCV
}

double FDisplayClusterLightCardEditorViewportClient::CalculateFinalLightCardDistance(double FlushDistance, double DesiredOffsetFromFlush) const
{
	double Distance = FMath::Min(FlushDistance, RootActorBoundingRadius) + DesiredOffsetFromFlush;

	return FMath::Max(Distance, 0);
}

void FDisplayClusterLightCardEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	// Don't select light cards while drawing a new light card
	if (InputMode == EInputMode::DrawingLightCard)
	{
		FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
		return;
	}

	const bool bIsCtrlKeyDown = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
	const bool bMultiSelect = Key == EKeys::LeftMouseButton && bIsCtrlKeyDown;
	const bool bIsRightClickSelection = Key == EKeys::RightMouseButton && !bIsCtrlKeyDown && !Viewport->KeyState(EKeys::LeftMouseButton);

	if (HitProxy)
	{
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			HActor* ActorHitProxy = static_cast<HActor*>(HitProxy);
			if (ActorHitProxy->Actor == RootActorProxy.Get())
			{
				if (ActorHitProxy->PrimComponent && ActorHitProxy->PrimComponent->IsA<UStaticMeshComponent>())
				{
					ADisplayClusterLightCardActor* TracedLightCard = TraceScreenForLightCard(View, HitX, HitY);
					SelectLightCard(TracedLightCard, bMultiSelect);
				}
			}
			else if (ActorHitProxy->Actor->IsA<ADisplayClusterLightCardActor>() && LightCardProxies.Contains(ActorHitProxy->Actor))
			{
				SelectLightCard(Cast<ADisplayClusterLightCardActor>(ActorHitProxy->Actor), bMultiSelect);
			}
			else if (!bMultiSelect)
			{
				SelectLightCard(nullptr);
			}
		}
	}
	else
	{
		SelectLightCard(nullptr);
	}
	
	PropagateLightCardSelection();

	if (bIsRightClickSelection)
	{
		LightCardEditorViewportPtr.Pin()->SummonContextMenu();
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
	else if (InViewport->IsCursorVisible() && !bWidgetAxisControlledByDrag)
	{
		EditorWidget->SetHighlightedAxis(EAxisList::Type::None);

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
						FSceneView* View = CalcSceneView(&ViewFamily);

						ADisplayClusterLightCardActor* TracedLightCard = TraceScreenForLightCard(*View, X, Y);
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
			else if (HitProxy->IsA(HWidgetAxis::StaticGetType()))
			{
				HWidgetAxis* AxisHitProxy = static_cast<HWidgetAxis*>(HitProxy);
				
				MouseCursor = AxisHitProxy->GetMouseCursor();
				EditorWidget->SetHighlightedAxis(AxisHitProxy->Axis);
			}
		}
	}

	CachedMouseX = X;
	CachedMouseY = Y;

	return MouseCursor;
}

void FDisplayClusterLightCardEditorViewportClient::DestroyDropPreviewActors()
{
	if (HasDropPreviewActors())
	{
		for (auto ActorIt = DropPreviewLightCards.CreateConstIterator(); ActorIt; ++ActorIt)
		{
			ADisplayClusterLightCardActor* PreviewActor = (*ActorIt).Get();
			if (PreviewActor)
			{
				IDisplayClusterScenePreview::Get().RemoveActorFromRenderer(PreviewRendererId, PreviewActor);
				GetWorld()->DestroyActor(PreviewActor);
			}
		}
		DropPreviewLightCards.Empty();
	}
}

bool FDisplayClusterLightCardEditorViewportClient::UpdateDropPreviewActors(int32 MouseX, int32 MouseY,
                                                                           const TArray<UObject*>& DroppedObjects, bool& bOutDroppedObjectsVisible, UActorFactory* FactoryToUse)
{
	bOutDroppedObjectsVisible = false;
	if(!HasDropPreviewActors())
	{
		return false;
	}

	bNeedsRedraw = true;

	const FIntPoint NewMousePos { MouseX, MouseY };

	for (TWeakObjectPtr<ADisplayClusterLightCardActor>& LightCard : DropPreviewLightCards)
	{
		if (LightCard.IsValid())
		{
			ProjectionHelper->VerifyAndFixLightCardOrigin(*LightCard);
		}
	}
	
	MoveLightCardsToPixel(NewMousePos, DropPreviewLightCards);
	
	return true;
}

bool FDisplayClusterLightCardEditorViewportClient::DropObjectsAtCoordinates(int32 MouseX, int32 MouseY,
                                                                            const TArray<UObject*>& DroppedObjects, TArray<AActor*>& OutNewActors, bool bOnlyDropOnTarget,
                                                                            bool bCreateDropPreview, bool bSelectActors, UActorFactory* FactoryToUse)
{
	if (!LightCardEditorPtr.IsValid() || LightCardEditorPtr.Pin()->GetActiveRootActor() == nullptr)
	{
		return false;
	}
	
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	check(PreviewWorld);
	
	DestroyDropPreviewActors();
	
	Viewport->InvalidateHitProxy();

	bool bSuccess = false;

	if (bSelectActors)
	{
		SelectedLightCards.Empty();
	}
	
	TArray<TWeakObjectPtr<ADisplayClusterLightCardActor>> CreatedLightCards;
	CreatedLightCards.Reserve(DroppedObjects.Num());

	SelectLightCard(nullptr);
	PropagateLightCardSelection();
	
	for (UObject* DroppedObject : DroppedObjects)
	{
		if (UDisplayClusterLightCardTemplate* Template = Cast<UDisplayClusterLightCardTemplate>(DroppedObject))
		{
			ADisplayClusterLightCardActor* LightCardActor = LightCardEditorPtr.Pin()->SpawnLightCardFromTemplate(Template,
				bCreateDropPreview ? PreviewWorld->GetCurrentLevel() : nullptr, bCreateDropPreview);
			check(LightCardActor);

			ProjectionHelper->VerifyAndFixLightCardOrigin(*LightCardActor);
			
			CreatedLightCards.Add(LightCardActor);
			
			if (bCreateDropPreview)
			{
				DropPreviewLightCards.Add(LightCardActor);
				
				LightCardActor->bIsProxy = true;
				IDisplayClusterScenePreview::Get().AddActorToRenderer(PreviewRendererId, LightCardActor);
			}
			else
			{
				LightCardActor->UpdatePolygonTexture();
				LightCardActor->UpdateLightCardMaterialInstance();
				if (bSelectActors)
				{
					GetOnNextSceneRefresh().AddLambda([this, LightCardActor]()
					{
						// Select on next refresh so the persistent level and corresponding proxy has spawned.
						SelectLightCard(LightCardActor, true);
						PropagateLightCardSelection();
					});
				}
			}
			
			bSuccess = true;
		}
	}

	const FIntPoint NewMousePos { MouseX, MouseY };
	MoveLightCardsToPixel(NewMousePos, CreatedLightCards);

	return bSuccess;
}

void FDisplayClusterLightCardEditorViewportClient::UpdatePreviewActor(ADisplayClusterRootActor* RootActor, bool bForce,
                                                                      EDisplayClusterLightCardEditorProxyType ProxyType)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FDisplayClusterLightCardEditorViewportClient::UpdatePreviewActor"), STAT_UpdatePreviewActor, STATGROUP_NDisplayLightCardEditor);
	
	if ((!bForce && RootActor == RootActorLevelInstance.Get()) ||
		(ProxyTypesRefreshing.Contains(ProxyType) || ProxyTypesRefreshing.Contains(EDisplayClusterLightCardEditorProxyType::All)))
	{
		return;
	}

	ProxyTypesRefreshing.Add(ProxyType);

	auto Finalize = [this, ProxyType]()
	{
		Viewport->InvalidateHitProxy();
		bShouldCheckHitProxy = true;

		ProxyTypesRefreshing.Remove(ProxyType);
		OnNextSceneRefreshDelegate.Broadcast();
		OnNextSceneRefreshDelegate.Clear();
	};
	
	if (RootActor == nullptr)
	{
		DestroyProxies(ProxyType);
		Finalize();
	}
	else
	{
		UWorld* PreviewWorld = PreviewScene->GetWorld();
		check(PreviewWorld);

		const TWeakObjectPtr<ADisplayClusterRootActor> RootActorPtr (RootActor);
		
		// Schedule for the next tick so CDO changes get propagated first in the event of config editor skeleton
		// regeneration & compiles. nDisplay's custom propagation may have issues if the archetype isn't correct.
		RefreshTimerHandle = PreviewWorld->GetTimerManager().SetTimerForNextTick([=]()
		{			
			DestroyProxies(ProxyType);

			if (!RootActorPtr.IsValid())
			{
				Finalize();
				return;
			}
			
			RootActorPtr->SubscribeToPostProcessRenderTarget(reinterpret_cast<uint8*>(this));
			RootActorLevelInstance = RootActorPtr;
			
			if (ProxyType == EDisplayClusterLightCardEditorProxyType::All ||
				ProxyType == EDisplayClusterLightCardEditorProxyType::RootActor)
			{
				{
					FObjectDuplicationParameters DupeActorParameters(RootActorPtr.Get(), PreviewWorld->GetCurrentLevel());
					DupeActorParameters.FlagMask = RF_AllFlags & ~(RF_ArchetypeObject | RF_Transactional); // Keeps archetypes correct in config data.
					DupeActorParameters.PortFlags = PPF_DuplicateVerbatim;
			
					RootActorProxy = CastChecked<ADisplayClusterRootActor>(StaticDuplicateObjectEx(DupeActorParameters));
				}
			
				PreviewWorld->GetCurrentLevel()->AddLoadedActor(RootActorProxy.Get());

				// Spawned actor will take the transform values from the template, so manually reset them to zero here
				RootActorProxy->SetActorLocation(FVector::ZeroVector);
				RootActorProxy->SetActorRotation(FRotator::ZeroRotator);

				ProjectionOriginComponent = FindProjectionOriginComponent(RootActorProxy.Get());

				RootActorProxy->UpdatePreviewComponents();
				RootActorProxy->EnableEditorRender(false);

				ProjectionHelper->SetLevelInstanceRootActor(*RootActorLevelInstance);
				ProjectionHelper->SetEditorViewportClient(AsShared());

				if (UDisplayClusterConfigurationData* ProxyConfig = RootActorProxy->GetConfigData())
				{
					// Disable lightcards so that it doesn't try to update the ones in the level instance world.
					ProxyConfig->StageSettings.Lightcard.bEnable = false;
				}

				FBox BoundingBox = RootActorProxy->GetComponentsBoundingBox();
				RootActorBoundingRadius = FMath::Max(BoundingBox.Min.Length(), BoundingBox.Max.Length());
			}

			// Filter out any primitives hidden in game except screen components
			IDisplayClusterScenePreview::Get().SetRendererRootActor(PreviewRendererId, RootActorProxy.Get());

			if (ProxyType == EDisplayClusterLightCardEditorProxyType::All ||
				ProxyType == EDisplayClusterLightCardEditorProxyType::LightCards)
			{
				TSet<ADisplayClusterLightCardActor*> LightCards;
				UDisplayClusterBlueprintLib::FindLightCardsForRootActor(RootActorPtr.Get(), LightCards);
				
				SelectLightCard(nullptr);
				
				for (ADisplayClusterLightCardActor* LightCard : LightCards)
				{
					FObjectDuplicationParameters DupeActorParameters(LightCard, PreviewWorld->GetCurrentLevel());
					DupeActorParameters.FlagMask = RF_AllFlags & ~(RF_ArchetypeObject | RF_Transactional);
					DupeActorParameters.PortFlags = PPF_DuplicateVerbatim;
				
					ADisplayClusterLightCardActor* LightCardProxy = CastChecked<ADisplayClusterLightCardActor>(StaticDuplicateObjectEx(DupeActorParameters));
					PreviewWorld->GetCurrentLevel()->AddLoadedActor(LightCardProxy);
				
					LightCardProxy->SetActorLocation(LightCard->GetActorLocation() - RootActorPtr->GetActorLocation());
					LightCardProxy->SetActorRotation(LightCard->GetActorRotation() - RootActorPtr->GetActorRotation());
					LightCardProxy->PolygonMask = LightCard->PolygonMask;
					LightCardProxy->bIsProxy = true;

					// Change mesh to proxy mesh with more vertices
					if (const UStaticMesh* LightCardMesh = LightCardProxy->GetStaticMesh())
					{
						// Only change the mesh if we are using the default one.
					
						const FString DefaultPlanePath = TEXT("/nDisplay/LightCard/SM_LightCardPlane.SM_LightCardPlane");
						const UStaticMesh* DefaultPlane = Cast<UStaticMesh>(FSoftObjectPath(DefaultPlanePath).TryLoad());

						if (DefaultPlane == LightCardMesh)
						{
							const FString LightCardPlanePath = TEXT("/nDisplay/LightCard/SM_LightCardPlaneSubdivided.SM_LightCardPlaneSubdivided");
							if (UStaticMesh* LightCardPlaneMesh = Cast<UStaticMesh>(FSoftObjectPath(LightCardPlanePath).TryLoad()))
							{
								LightCardProxy->SetStaticMesh(LightCardPlaneMesh);
							}
						}
					}

					LightCardProxies.Add(FLightCardProxy(LightCard, LightCardProxy));
				}

				// Update the selected light card proxies to match the currently selected light cards in the light card list
				TArray<ADisplayClusterLightCardActor*> CurrentlySelectedLightCards;
				if (LightCardEditorPtr.IsValid())
				{
					LightCardEditorPtr.Pin()->GetSelectedLightCards(CurrentlySelectedLightCards);
				}

				SelectLightCards(CurrentlySelectedLightCards);
			}

			for (const FLightCardProxy& LightCardProxy : LightCardProxies)
			{
				IDisplayClusterScenePreview::Get().AddActorToRenderer(PreviewRendererId, LightCardProxy.Proxy.Get(), [this](const UPrimitiveComponent* PrimitiveComponent)
				{
					// Always add the light card mesh component to the renderer's scene even if it is marked hidden in game, since UV light cards will purposefully
					// hide the light card mesh since it isn't supposed to exist in 3D space. The light card mesh will be appropriately filtered when the scene is
					// rendered based on the projection mode
					if (PrimitiveComponent->GetFName() == TEXT("LightCard"))
					{
						return true;
					}

					return !PrimitiveComponent->bHiddenInGame;
				});
			}

			Finalize();
		});
	}
}

void FDisplayClusterLightCardEditorViewportClient::UpdateProxyTransforms()
{
	if (RootActorLevelInstance.IsValid())
	{
		if (RootActorProxy.IsValid())
		{
			// Only update scale for the root actor.
			RootActorProxy->SetActorScale3D(RootActorLevelInstance->GetActorScale3D());
		}
		
		for (const FLightCardProxy& LightCardProxy : LightCardProxies)
		{
			if (LightCardProxy.LevelInstance.IsValid() && LightCardProxy.Proxy.IsValid())
			{
				LightCardProxy.Proxy->SetActorLocation(LightCardProxy.LevelInstance->GetActorLocation() - RootActorLevelInstance->GetActorLocation());
				LightCardProxy.Proxy->SetActorRotation(LightCardProxy.LevelInstance->GetActorRotation() - RootActorLevelInstance->GetActorRotation());
				LightCardProxy.Proxy->SetActorScale3D(LightCardProxy.LevelInstance->GetActorScale3D());
			}
		}
	}
}

void FDisplayClusterLightCardEditorViewportClient::DestroyProxies(
	EDisplayClusterLightCardEditorProxyType ProxyType)
{
	// Clear the primitives from the scene renderer based on the type of proxy that is being destroyed
	switch (ProxyType)
	{
	case EDisplayClusterLightCardEditorProxyType::RootActor:
		if (RootActorProxy.IsValid())
		{
			IDisplayClusterScenePreview::Get().RemoveActorFromRenderer(PreviewRendererId, RootActorProxy.Get());
		}
		break;

	case EDisplayClusterLightCardEditorProxyType::LightCards:
		for (const FLightCardProxy& LightCardProxy : LightCardProxies)
		{
			if (LightCardProxy.Proxy.IsValid())
			{
				IDisplayClusterScenePreview::Get().RemoveActorFromRenderer(PreviewRendererId, LightCardProxy.Proxy.Get());
			}
		}
		break;

	case EDisplayClusterLightCardEditorProxyType::All:
	default:
		IDisplayClusterScenePreview::Get().ClearRendererScene(PreviewRendererId);
		break;
	}

	UWorld* PreviewWorld = PreviewScene->GetWorld();
	check(PreviewWorld);
	
	if (ProxyType == EDisplayClusterLightCardEditorProxyType::All ||
		ProxyType == EDisplayClusterLightCardEditorProxyType::RootActor)
	{
		if (RootActorProxy.IsValid())
		{
			PreviewWorld->EditorDestroyActor(RootActorProxy.Get(), false);
			RootActorProxy.Reset();
		}

		if (RootActorLevelInstance.IsValid())
		{
			RootActorLevelInstance->UnsubscribeFromPostProcessRenderTarget(reinterpret_cast<uint8*>(this));
			RootActorLevelInstance.Reset();
		}
	}
	
	if (ProxyType == EDisplayClusterLightCardEditorProxyType::All ||
		ProxyType == EDisplayClusterLightCardEditorProxyType::LightCards)
	{
		for (const FLightCardProxy& LightCardProxy : LightCardProxies)
		{
			if (LightCardProxy.Proxy.IsValid())
			{
				PreviewWorld->EditorDestroyActor(LightCardProxy.Proxy.Get(), false);
			}
		}

		LightCardProxies.Empty();	
	}
}

void FDisplayClusterLightCardEditorViewportClient::SelectLightCards(const TArray<ADisplayClusterLightCardActor*>& LightCardsToSelect)
{
	SelectLightCard(nullptr);
	for (AActor* LightCard : LightCardsToSelect)
	{
		if (FLightCardProxy* FoundProxy = LightCardProxies.FindByKey(LightCard))
		{
			if (FoundProxy->Proxy.IsValid())
			{
				SelectLightCard(FoundProxy->Proxy.Get(), true);
			}
		}
	}
}

void FDisplayClusterLightCardEditorViewportClient::SetProjectionMode(EDisplayClusterMeshProjectionType InProjectionMode, ELevelViewportType InViewportType)
{
	ProjectionMode = InProjectionMode;
	RenderViewportType = InViewportType;

	ProjectionHelper->SetProjectionMode(ProjectionMode);
	ProjectionHelper->SetIsOrthographic(InViewportType != LVT_Perspective);

	if (ProjectionMode == EDisplayClusterMeshProjectionType::Linear || ProjectionMode == EDisplayClusterMeshProjectionType::UV)
	{
		// TODO: Do we want to cache the perspective rotation and restore it when the user switches back?
		SetViewRotation(FVector::ForwardVector.Rotation());
		EditorWidget->SetWidgetScale(1.f);
	}
	else if (ProjectionMode == EDisplayClusterMeshProjectionType::Azimuthal)
	{
		SetViewRotation(FVector::UpVector.Rotation());
		EditorWidget->SetWidgetScale(0.5f);
	}

	ProjectionOriginComponent = FindProjectionOriginComponent(RootActorProxy.Get());

	if (Viewport)
	{
		Viewport->InvalidateHitProxy();
	}

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

	Viewport->InvalidateHitProxy();
	bShouldCheckHitProxy = true;
}

void FDisplayClusterLightCardEditorViewportClient::ResetCamera(bool bLocationOnly)
{
	FVector Location = FVector::ZeroVector;
	if (ProjectionOriginComponent.IsValid())
	{
		Location = ProjectionOriginComponent->GetComponentLocation();
	}

	SetViewLocation(Location);

	if (bLocationOnly)
	{
		return;
	}
	
	SetProjectionMode(GetProjectionMode(), GetRenderViewportType());

	ResetFOVs();
}

void FDisplayClusterLightCardEditorViewportClient::MoveLightCardTo(ADisplayClusterLightCardActor& LightCard, const FDisplayClusterLightCardEditorHelper::FSphericalCoordinates& SphericalCoords) const
{
	const FVector Origin = GetViewLocation(); // Assumed the same as the LC origin in proxy space. Call VerifyAndFixLightCardOrigin to ensure this.

	ProjectionHelper->MoveLightCardsTo({ &LightCard }, SphericalCoords);
}

void FDisplayClusterLightCardEditorViewportClient::CenterLightCardInView(ADisplayClusterLightCardActor& LightCard)
{
	ProjectionHelper->VerifyAndFixLightCardOrigin(LightCard);

	if (LightCard.bIsUVLightCard)
	{
		LightCard.UVCoordinates = FVector2D(0.5, 0.5);
	}
	else
	{
		MoveLightCardTo(LightCard, FDisplayClusterLightCardEditorHelper::FSphericalCoordinates(GetViewRotation().RotateVector(FVector::ForwardVector)));
	}

	// If this is a proxy light, propagate to its counterpart in the level.
	if (LightCard.bIsProxy)
	{
		PropagateLightCardTransform(&LightCard);
	}
}

void FDisplayClusterLightCardEditorViewportClient::MoveSelectedLightCardsToPixel(const FIntPoint& PixelPos)
{
	MoveLightCardsToPixel(PixelPos, SelectedLightCards);
}

void FDisplayClusterLightCardEditorViewportClient::BeginTransaction(const FText& Description)
{
	GEditor->BeginTransaction(Description);
}

void FDisplayClusterLightCardEditorViewportClient::EndTransaction()
{
	GEditor->EndTransaction();
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
	const FViewportCameraTransform& ViewTransform = GetViewTransform();
	const FRotator& ViewRotation = ViewTransform.GetRotation();
	const FMatrix RotationMatrix = CalcViewRotationMatrix(ViewRotation);

	FSceneViewInitOptions ViewInitOptions;
	ProjectionHelper->GetSceneViewInitOptions(
		ViewInitOptions,
		GetProjectionModeFOV(ProjectionMode),
		Viewport->GetSizeXY(),
		ViewTransform.GetLocation(),
		ViewTransform.GetRotation(),
		GetDefault<ULevelEditorViewportSettings>()->AspectRatioAxisConstraint,
		GetNearClipPlane(),
		&RotationMatrix,
		GetDPIScale()
	);

	ViewInitOptions.SceneViewStateInterface = ViewState.GetReference();
	ViewInitOptions.ViewElementDrawer = this;

	ViewInitOptions.BackgroundColor = GetBackgroundColor();

	ViewInitOptions.EditorViewBitflag = (uint64)1 << ViewIndex, // send the bit for this view - each actor will check it's visibility bits against this

	ViewInitOptions.OverrideFarClippingPlaneDistance = GetFarClipPlaneOverride();
	ViewInitOptions.CursorPos = CurrentMousePos;

	OutViewInitOptions = ViewInitOptions;
}

void FDisplayClusterLightCardEditorViewportClient::GetNormalMapSceneViewInitOptions(FIntPoint NormalMapSize, float NormalMapFOV, const FVector& ViewDirection, FSceneViewInitOptions& OutViewInitOptions)
{
	FViewportCameraTransform& ViewTransform = GetViewTransform();

	OutViewInitOptions.ViewLocation = ProjectionOriginComponent.IsValid() ? ProjectionOriginComponent->GetComponentLocation() : FVector::ZeroVector;
	OutViewInitOptions.ViewRotation = ViewDirection.Rotation();
	OutViewInitOptions.ViewOrigin = OutViewInitOptions.ViewLocation;

	OutViewInitOptions.SetViewRectangle(FIntRect(0, 0, NormalMapSize.X, NormalMapSize.Y));

	AWorldSettings* WorldSettings = nullptr;
	if (GetScene() != nullptr && GetScene()->GetWorld() != nullptr)
	{
		WorldSettings = GetScene()->GetWorld()->GetWorldSettings();
	}

	if (WorldSettings != nullptr)
	{
		OutViewInitOptions.WorldToMetersScale = WorldSettings->WorldToMeters;
	}

	// Rotate view 90 degrees
	OutViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(OutViewInitOptions.ViewRotation) * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	const float MinZ = GetNearClipPlane();
	const float MaxZ = FMath::Max(RootActorBoundingRadius, MinZ);

	// Avoid zero ViewFOV's which cause divide by zero's in projection matrix
	const float MatrixFOV = FMath::Max(0.001f, NormalMapFOV) * (float)PI / 360.0f;

	const float XAxisMultiplier = 1.0f;
	const float YAxisMultiplier = 1.0f;

	if ((bool)ERHIZBuffer::IsInverted)
	{
		OutViewInitOptions.ProjectionMatrix = FReversedZPerspectiveMatrix(
			MatrixFOV,
			MatrixFOV,
			XAxisMultiplier,
			YAxisMultiplier,
			MinZ,
			MaxZ);
	}
	else
	{
		OutViewInitOptions.ProjectionMatrix = FPerspectiveMatrix(
			MatrixFOV,
			MatrixFOV,
			XAxisMultiplier,
			YAxisMultiplier,
			MinZ,
			MaxZ);
	}

	OutViewInitOptions.SceneViewStateInterface = ViewState.GetReference();
	OutViewInitOptions.ViewElementDrawer = this;

	OutViewInitOptions.EditorViewBitflag = (uint64)1 << ViewIndex, // send the bit for this view - each actor will check it's visibility bits against this

	OutViewInitOptions.FOV = NormalMapFOV;
	OutViewInitOptions.OverrideFarClippingPlaneDistance = GetFarClipPlaneOverride();
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

USceneComponent* FDisplayClusterLightCardEditorViewportClient::FindProjectionOriginComponent(const ADisplayClusterRootActor* InRootActor) const
{
	if (!InRootActor)
	{
		return nullptr;
	}

	return InRootActor->GetCommonViewPoint();
}

bool FDisplayClusterLightCardEditorViewportClient::IsLightCardSelected(const AActor* Actor)
{
	return SelectedLightCards.Contains(Actor);
}

void FDisplayClusterLightCardEditorViewportClient::SelectLightCard(ADisplayClusterLightCardActor* Actor, bool bAddToSelection)
{
	TArray<ADisplayClusterLightCardActor*> UpdatedActors;

	if (!bAddToSelection)
	{
		for (const TWeakObjectPtr<ADisplayClusterLightCardActor>& LightCard : SelectedLightCards)
		{
			if (LightCard.IsValid())
			{
				UpdatedActors.Add(LightCard.Get());
			}
		}

		SelectedLightCards.Empty();
		LastSelectedLightCard = nullptr;
	}

	if (Actor)
	{
		SelectedLightCards.Add(Actor);
		UpdatedActors.Add(Actor);
		LastSelectedLightCard = Actor;
	}

	for (AActor* UpdatedActor : UpdatedActors)
	{
		UpdatedActor->PushSelectionToProxies();
	}
}

void FDisplayClusterLightCardEditorViewportClient::PropagateLightCardSelection()
{
	TArray<ADisplayClusterLightCardActor*> SelectedLevelInstances;
	for (const TWeakObjectPtr<ADisplayClusterLightCardActor>& SelectedLightCard : SelectedLightCards)
	{
		if (FLightCardProxy* FoundProxy = LightCardProxies.FindByKey(SelectedLightCard.Get()))
		{
			if (FoundProxy->LevelInstance.IsValid())
			{
				SelectedLevelInstances.Add(FoundProxy->LevelInstance.Get());
			}
		}
	}

	LightCardEditorPtr.Pin()->SelectLightCards(SelectedLevelInstances);
}

void FDisplayClusterLightCardEditorViewportClient::PropagateLightCardTransform(ADisplayClusterLightCardActor* LightCardProxy)
{
	const FLightCardProxy* FoundProxy = LightCardProxies.FindByKey(LightCardProxy);
	if (FoundProxy && FoundProxy->Proxy == LightCardProxy && FoundProxy->LevelInstance.IsValid())
	{
		ADisplayClusterLightCardActor* LevelInstance = FoundProxy->LevelInstance.Get();

		LevelInstance->Modify();

		TArray<const FProperty*> ChangedProperties;
		
		// Set the level instance property value to our proxy property value.
		auto TryChangeProperty = [&](FName InPropertyName) -> void
		{
			const FProperty* Property = FindFProperty<FProperty>(LevelInstance->GetClass(), InPropertyName);
			check(Property);
			
			// Only change if values are different.
			if (!Property->Identical_InContainer(LightCardProxy, LevelInstance))
			{
				void* NewValue = nullptr;
				Property->GetValue_InContainer(LightCardProxy, &NewValue);
				Property->SetValue_InContainer(LevelInstance, &NewValue);

				ChangedProperties.Add(Property);
			}
		};
		
		// Here we count on the fact that the root actor proxy has zero loc/rot.
		// If that ever changes then the math below will need to be updated to use the 
		// relative loc/rot of the LC proxies wrt the root actor proxy.

		const FVector RootActorLevelInstanceLocation = RootActorLevelInstance.IsValid() ? RootActorLevelInstance->GetActorLocation() : FVector::ZeroVector;
		LevelInstance->SetActorLocation(RootActorLevelInstanceLocation + LightCardProxy->GetActorLocation());

		const FRotator RootActorLevelInstanceRotation = RootActorLevelInstance.IsValid() ? RootActorLevelInstance->GetActorRotation() : FRotator::ZeroRotator;
		LevelInstance->SetActorRotation(RootActorLevelInstanceRotation.Quaternion() * LightCardProxy->GetActorRotation().Quaternion());

		TryChangeProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterLightCardActor, Longitude));
		TryChangeProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterLightCardActor, Latitude));
		TryChangeProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterLightCardActor, DistanceFromCenter));
		TryChangeProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterLightCardActor, Spin));
		TryChangeProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterLightCardActor, Pitch));
		TryChangeProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterLightCardActor, Yaw));
		TryChangeProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterLightCardActor, Scale));
		TryChangeProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterLightCardActor, UVCoordinates));
		
		// Snapshot the changed properties so multi-user can update while dragging.
		if (ChangedProperties.Num() > 0)
		{
			SnapshotTransactionBuffer(LevelInstance, MakeArrayView(ChangedProperties));
		}
	}
}

void FDisplayClusterLightCardEditorViewportClient::MoveSelectedLightCards(FViewport* InViewport, EAxisList::Type CurrentAxis)
{
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		InViewport,
		GetScene(),
		EngineShowFlags)
		.SetRealtimeUpdate(IsRealtime())
	);

	FSceneView* View = CalcSceneView(&ViewFamily);

	FIntPoint MousePos;
	InViewport->GetMousePos(MousePos);

	// Move the light cards
	ProjectionHelper->DragLightCards(SelectedLightCards, MousePos, *View, DragWidgetOffset, CurrentAxis, LastSelectedLightCard.Get());

	// Update the level instances
	for (const TWeakObjectPtr<ADisplayClusterLightCardActor>& LightCard : SelectedLightCards)
	{
		PropagateLightCardTransform(LightCard.Get());
	}

	FSceneViewInitOptions SceneViewInitOptions;
	GetSceneViewInitOptions(SceneViewInitOptions);
	FViewMatrices ViewMatrices(SceneViewInitOptions);
	
	FVector LightCardProjectedLocation = ProjectWorldPosition(CachedEditorWidgetWorldTransform.GetTranslation(), ViewMatrices);
	FVector2D ScreenPercentage;
	
	if (IsLocationCloseToEdge(LightCardProjectedLocation, InViewport, View, &ScreenPercentage))
	{
		DesiredLookAtSpeed = FMath::Max(ScreenPercentage.X, ScreenPercentage.Y) * MaxDesiredLookAtSpeed;
		DesiredLookAtLocation = LightCardProjectedLocation;
	}
	else
	{
		DesiredLookAtLocation.Reset();
	}
}

void FDisplayClusterLightCardEditorViewportClient::MoveLightCardsToPixel(const FIntPoint& PixelPos, const TArray<TWeakObjectPtr<ADisplayClusterLightCardActor>>& InLightCards)
{
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		GetScene(),
		EngineShowFlags)
		.SetRealtimeUpdate(IsRealtime())
	);

	FSceneView* View = CalcSceneView(&ViewFamily);

	ProjectionHelper->MoveLightCardsToPixel(SelectedLightCards, PixelPos, *View);

	// Update each light card with the delta coordinates; the flush constraint is applied by MoveLightCardTo, ensuring the light card is always flush to screens
	for (const TWeakObjectPtr<ADisplayClusterLightCardActor>& LightCard : InLightCards)
	{
		if (LightCard.IsValid() &&
			((LightCard->bIsUVLightCard && ProjectionMode == EDisplayClusterMeshProjectionType::UV) ||
			(!LightCard->bIsUVLightCard && ProjectionMode != EDisplayClusterMeshProjectionType::UV)))
		{
			PropagateLightCardTransform(LightCard.Get());
		}
	}
}


void FDisplayClusterLightCardEditorViewportClient::MoveSelectedUVLightCards(FViewport* InViewport, EAxisList::Type CurrentAxis)
{
	if (LastSelectedLightCard.IsValid() && LastSelectedLightCard->bIsUVLightCard)
	{
		const FVector2D DeltaUV = GetUVLightCardTranslationDelta(InViewport, LastSelectedLightCard.Get(), CurrentAxis);
		for (const TWeakObjectPtr<ADisplayClusterLightCardActor>& LightCard : SelectedLightCards)
		{
			if (!LightCard.IsValid() || !LightCard->bIsUVLightCard)
			{
				continue;
			}

			LightCard->UVCoordinates += DeltaUV;

			PropagateLightCardTransform(LightCard.Get());
		}
	}
}

FVector2D FDisplayClusterLightCardEditorViewportClient::GetUVLightCardTranslationDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard, EAxisList::Type CurrentAxis)
{
	FIntPoint MousePos;
	InViewport->GetMousePos(MousePos);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		InViewport,
		GetScene(),
		EngineShowFlags)
		.SetRealtimeUpdate(IsRealtime()));
	FSceneView* View = CalcSceneView(&ViewFamily);

	FVector Origin;
	FVector Direction;
	ProjectionHelper->PixelToWorld(*View, MousePos, Origin, Direction);

	const float UVProjectionPlaneSize = ADisplayClusterLightCardActor::UVPlaneDefaultSize;
	const float UVProjectionPlaneDistance = ADisplayClusterLightCardActor::UVPlaneDefaultDistance;

	const FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
	const FPlane UVProjectionPlane(ViewOrigin + FVector::ForwardVector * UVProjectionPlaneDistance, -FVector::ForwardVector);
	const FVector PlaneIntersection = FMath::RayPlaneIntersection(Origin, Direction, UVProjectionPlane);

	const FVector DesiredLocation = ((PlaneIntersection - DragWidgetOffset) - ViewOrigin);
	const FVector2D DesiredUVLocation = FVector2D(DesiredLocation.Y / UVProjectionPlaneSize + 0.5f, 0.5f - DesiredLocation.Z / UVProjectionPlaneSize);

	const FVector2D UVDelta = DesiredUVLocation - LightCard->UVCoordinates;

	FVector2D UVAxis = FVector2D::ZeroVector;
	if (CurrentAxis & EAxisList::Type::X)
	{
		UVAxis += FVector2D(1.0, 0.0);
	}

	if (CurrentAxis & EAxisList::Type::Y)
	{
		UVAxis += FVector2D(0.0, 1.0);
	}

	return UVDelta * UVAxis;
}

void FDisplayClusterLightCardEditorViewportClient::ScaleSelectedLightCards(FViewport* InViewport, EAxisList::Type CurrentAxis)
{
	if (LastSelectedLightCard.IsValid())
	{
		const FVector2D DeltaScale = GetLightCardScaleDelta(InViewport, LastSelectedLightCard.Get(), CurrentAxis);
		for (const TWeakObjectPtr<ADisplayClusterLightCardActor>& LightCard : SelectedLightCards)
		{
			if (!LightCard.IsValid())
			{
				continue;
			}

			LightCard->Scale += DeltaScale;

			PropagateLightCardTransform(LightCard.Get());
		}
	}
}

FVector2D FDisplayClusterLightCardEditorViewportClient::GetLightCardScaleDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard, EAxisList::Type CurrentAxis)
{
	FIntPoint MousePos;
	InViewport->GetMousePos(MousePos);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		InViewport,
		GetScene(),
		EngineShowFlags)
		.SetRealtimeUpdate(IsRealtime()));
	FSceneView* View = CalcSceneView(&ViewFamily);

	const FVector2D DragDir = MousePos - LastWidgetMousePos;

	const FVector WorldWidgetOrigin = CachedEditorWidgetWorldTransform.GetTranslation();
	const FVector WorldWidgetXAxis = CachedEditorWidgetWorldTransform.GetRotation().RotateVector(FVector::XAxisVector);
	const FVector WorldWidgetYAxis = CachedEditorWidgetWorldTransform.GetRotation().RotateVector(FVector::YAxisVector);

	FVector2D ScreenXAxis = FVector2D::ZeroVector;
	FVector2D ScreenYAxis = FVector2D::ZeroVector;

	WorldToScreenDirection(*View, WorldWidgetOrigin, WorldWidgetXAxis, ScreenXAxis);
	WorldToScreenDirection(*View, WorldWidgetOrigin, WorldWidgetYAxis, ScreenYAxis);

	FVector2D ScaleDir = FVector2D::ZeroVector;

	if (CurrentAxis & EAxisList::Type::X)
	{
		ScaleDir += ScreenXAxis;
	}

	if (CurrentAxis & EAxisList::Type::Y)
	{
		ScaleDir += ScreenYAxis;
	}

	ScaleDir.Normalize();

	// To give the scale delta a nice feel to it when dragging, we want the distance the user is dragging the mouse to be proportional to the change in size
	// of the light card when the scale delta is applied. So, if the user moves the mouse a distance d, then the light card's bounds along the direction of scale
	// should change by 2d (one d for each side), and the new scale should be s' = (h + 2d) / h_0, where h is the current length of the side and h_0 is the unscaled
	// length of the side. Since the current scale s = h / h_0, this means that the scale delta is s' - s = 2d / h_0.

	// First, obtain the size of the unscaled light card in the direction the user is scaling. Convert to screen space as the scale and drag vectors are in screen space
	const bool bLocalSpace = true;
	const FVector LightCardSize3D = LightCard->GetLightCardBounds(bLocalSpace).GetSize();
	const FVector2D SizeToScale = FVector2D((CurrentAxis & EAxisList::X) * LightCardSize3D.Y, (CurrentAxis & EAxisList::Y) * LightCardSize3D.Z);
	const double DistanceFromCamera = FMath::Max(FVector::Dist(WorldWidgetOrigin, View->ViewMatrices.GetViewOrigin()), 1.0f);
	const double ScreenSize = RenderViewportType == LVT_Perspective 
		? View->ViewMatrices.GetScreenScale() * SizeToScale.Length() / DistanceFromCamera 
		: SizeToScale.Length() * View->ViewMatrices.GetProjectionMatrix().M[0][0] * View->UnscaledViewRect.Width();

	// Compute the scale delta as s' - s = 2d / h_0
	const double ScaleMagnitude = 2.0f * (ScaleDir | DragDir) / ScreenSize;

	FVector2D ScaleDelta = FVector2D((CurrentAxis & EAxisList::X) * ScaleMagnitude, (CurrentAxis & EAxisList::Y) * ScaleMagnitude);

	// If both axes are being scaled at the same time, preserve the aspect ratio of the scale delta
	if ((CurrentAxis & EAxisList::Type::X) && (CurrentAxis & EAxisList::Type::Y))
	{
		// Ensure the signs of the deltas remain the same, and avoid potential divide by zero
		ScaleDelta.Y = ScaleDelta.X * FMath::Abs(LightCard->Scale.Y) / FMath::Max(0.001, FMath::Abs(LightCard->Scale.X));
	}

	return ScaleDelta;
}

void FDisplayClusterLightCardEditorViewportClient::SpinSelectedLightCards(FViewport* InViewport)
{
	if (LastSelectedLightCard.IsValid())
	{
		const double DeltaSpin = GetLightCardSpinDelta(InViewport, LastSelectedLightCard.Get());
		for (const TWeakObjectPtr<ADisplayClusterLightCardActor>& LightCard : SelectedLightCards)
		{
			if (!LightCard.IsValid())
			{
				continue;
			}

			LightCard->Spin += DeltaSpin;

			PropagateLightCardTransform(LightCard.Get());
		}
	}
}

double FDisplayClusterLightCardEditorViewportClient::GetLightCardSpinDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard)
{
	FIntPoint MousePos;
	InViewport->GetMousePos(MousePos);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		InViewport,
		GetScene(),
		EngineShowFlags)
		.SetRealtimeUpdate(IsRealtime()));
	FSceneView* View = CalcSceneView(&ViewFamily);

	const FVector WorldWidgetOrigin = CachedEditorWidgetWorldTransform.GetTranslation();
	const FVector WorldWidgetXAxis = CachedEditorWidgetWorldTransform.GetRotation().RotateVector(FVector::XAxisVector);
	const FVector WorldWidgetYAxis = CachedEditorWidgetWorldTransform.GetRotation().RotateVector(FVector::YAxisVector);

	FVector2D ScreenOrigin = FVector2D::ZeroVector;
	FVector2D ScreenXAxis = FVector2D::ZeroVector;
	FVector2D ScreenYAxis = FVector2D::ZeroVector;

	ProjectionHelper->WorldToPixel(*View, WorldWidgetOrigin, ScreenOrigin);
	WorldToScreenDirection(*View, WorldWidgetOrigin, WorldWidgetXAxis, ScreenXAxis);
	WorldToScreenDirection(*View, WorldWidgetOrigin, WorldWidgetYAxis, ScreenYAxis);

	FVector2D MousePosOffset = FVector2D(MousePos) - ScreenOrigin;
	FVector2D LastMousePosOffset = FVector2D(LastWidgetMousePos) - ScreenOrigin;

	double Theta = FMath::Atan2(MousePosOffset | ScreenYAxis, MousePosOffset | ScreenXAxis);
	double LastTheta = FMath::Atan2(LastMousePosOffset | ScreenYAxis, LastMousePosOffset | ScreenXAxis);

	return FMath::RadiansToDegrees(Theta - LastTheta);
}

ADisplayClusterLightCardActor* FDisplayClusterLightCardEditorViewportClient::TraceScreenForLightCard(const FSceneView& View, int32 HitX, int32 HitY)
{
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	check(PreviewWorld);

	FVector Origin;
	FVector Direction;
	ProjectionHelper->PixelToWorld(View, FIntPoint(HitX, HitY), Origin, Direction);

	const FVector CursorRayStart = Origin;
	const FVector CursorRayEnd = CursorRayStart + Direction * (RenderViewportType == LVT_Perspective ? HALF_WORLD_MAX : WORLD_MAX);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(LightCardTrace), true);

	bool bHitLightCard = false;
	FHitResult ScreenHitResult;
	if (PreviewWorld->LineTraceSingleByObjectType(ScreenHitResult, CursorRayStart, CursorRayEnd, FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllObjects), TraceParams))
	{
		if (AActor* HitActor = ScreenHitResult.GetActor())
		{
			if (RootActorProxy.Get() == HitActor && ScreenHitResult.Component.IsValid())
			{
				if (UDisplayClusterConfigurationViewport* CfgViewport = FindViewportForPrimitiveComponent(ScreenHitResult.Component.Get()))
				{
					FString ViewOriginName = CfgViewport->Camera;
					UDisplayClusterCameraComponent* ViewOrigin = nullptr;

					// If the view origin name is empty, use the first found view origin in the root actor
					if (ViewOriginName.IsEmpty())
					{
						ViewOrigin = RootActorProxy->GetDefaultCamera();
					}
					else
					{
						ViewOrigin = RootActorProxy->GetComponentByName<UDisplayClusterCameraComponent>(ViewOriginName);
					}

					if (ViewOrigin)
					{
						const FVector ViewOriginRayStart = ViewOrigin->GetComponentLocation();
						const FVector ViewOriginRayEnd = ViewOriginRayStart + (ScreenHitResult.Location - ViewOriginRayStart) * HALF_WORLD_MAX;

						TArray<FHitResult> HitResults;
						if (PreviewWorld->LineTraceMultiByObjectType(HitResults, ViewOriginRayStart, ViewOriginRayEnd, FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllObjects), TraceParams))
						{
							for (FHitResult& HitResult : HitResults)
							{
								if (ADisplayClusterLightCardActor* LightCardActor = Cast<ADisplayClusterLightCardActor>(HitResult.GetActor()))
								{
									if (LightCardProxies.Contains(LightCardActor))
									{
										return LightCardActor;
									}
								}
							}
						}
					}
				}
			}
			else if (HitActor->IsA<ADisplayClusterLightCardActor>() && LightCardProxies.Contains(HitActor))
			{
				return Cast<ADisplayClusterLightCardActor>(HitActor);
			}
		}
	}

	return nullptr;
}

FVector FDisplayClusterLightCardEditorViewportClient::ProjectWorldPosition(const FVector& UnprojectedWorldPosition, const FViewMatrices& ViewMatrices) const
{
	FDisplayClusterMeshProjectionTransform Transform(ProjectionMode, ViewMatrices.GetViewMatrix());
	return Transform.ProjectPosition(UnprojectedWorldPosition);
}

bool FDisplayClusterLightCardEditorViewportClient::WorldToScreenDirection(const FSceneView& View, const FVector& WorldPos, const FVector& WorldDirection, FVector2D& OutScreenDir)
{
	FVector2D ScreenVectorStart = FVector2D::ZeroVector;
	FVector2D ScreenVectorEnd = FVector2D::ZeroVector;

	if (ProjectionHelper->WorldToPixel(View, WorldPos, ScreenVectorStart)
		&& ProjectionHelper->WorldToPixel(View, WorldPos + WorldDirection, ScreenVectorEnd))
	{
		OutScreenDir = (ScreenVectorEnd - ScreenVectorStart).GetSafeNormal();
		return true;
	}
	else
	{
		// If either the start or end of the vector is not onscreen, translate the vector to be in front of the camera to approximate the screen space direction
		const FMatrix InvViewMatrix = View.ViewMatrices.GetInvViewMatrix();
		const FVector ViewLocation = InvViewMatrix.GetOrigin();
		const FVector ViewDirection = InvViewMatrix.GetUnitAxis(EAxis::Z);
		const FVector Offset = ViewDirection * (FVector::DotProduct(ViewLocation - WorldPos, ViewDirection) + 100.0f);
		const FVector AdjustedWorldPos = WorldPos + Offset;

		if (ProjectionHelper->WorldToPixel(View, AdjustedWorldPos, ScreenVectorStart)
			&& ProjectionHelper->WorldToPixel(View, AdjustedWorldPos + WorldDirection, ScreenVectorEnd))
		{
			OutScreenDir = -(ScreenVectorEnd - ScreenVectorStart).GetSafeNormal();
			return true;
		}
	}

	return false;
}

bool FDisplayClusterLightCardEditorViewportClient::CalcEditorWidgetTransform(FTransform& WidgetTransform)
{
	if (!SelectedLightCards.Num())
	{
		return false;
	}

	if (!LastSelectedLightCard.IsValid())
	{
		return false;
	}

	FVector LightCardPosition = LastSelectedLightCard->GetLightCardTransform().GetTranslation();

	WidgetTransform = FTransform(FRotator::ZeroRotator, LightCardPosition, FVector::OneVector);

	FQuat WidgetOrientation;
	if (EditorWidget->GetWidgetMode() == FDisplayClusterLightCardEditorWidget::EWidgetMode::WM_Translate)
	{
		if (ProjectionMode == EDisplayClusterMeshProjectionType::UV)
		{
			WidgetOrientation = FMatrix(FVector::YAxisVector, FVector::ZAxisVector, FVector::XAxisVector, FVector::ZeroVector).ToQuat();
		}
		else
		{
			// The translation widget should be oriented to show the x axis pointing in the longitudinal direction and the y axis pointing in the latitudinal direction
			const FVector ProjectionOrigin = ProjectionOriginComponent.IsValid() ? ProjectionOriginComponent->GetComponentLocation() : FVector::ZeroVector;
			const FVector RadialVector = (LightCardPosition - ProjectionOrigin).GetSafeNormal();
			const FVector AzimuthalVector = (FVector::ZAxisVector ^ RadialVector).GetSafeNormal();
			const FVector InclinationVector = RadialVector ^ AzimuthalVector;

			WidgetOrientation = FMatrix(AzimuthalVector, InclinationVector, RadialVector, FVector::ZeroVector).ToQuat();
		}
	}
	else
	{
		// Otherwise, orient the widget to match the light card's local orientation (spin, pitch, yaw)
		const FQuat LightCardOrientation = LastSelectedLightCard->GetLightCardTransform().GetRotation();
		const FVector Normal = LightCardOrientation.RotateVector(FVector::XAxisVector);
		const FVector Tangent = LightCardOrientation.RotateVector(FVector::YAxisVector);
		const FVector Binormal = LightCardOrientation.RotateVector(FVector::ZAxisVector);

		// Reorder the orientation basis so that the x axis and the y axis point along the light card's width and height, respectively
		WidgetOrientation = FMatrix(-Tangent, Binormal, -Normal, FVector::ZeroVector).ToQuat();
	}

	WidgetTransform.SetRotation(WidgetOrientation);

	return true;
}

bool FDisplayClusterLightCardEditorViewportClient::IsLocationCloseToEdge(const FVector& InPosition, const FViewport* InViewport,
                                                                         const FSceneView* InView, FVector2D* OutPercentageToEdge)
{
	if (InViewport == nullptr)
	{
		InViewport = Viewport;
	}

	check(InViewport);
	const FIntPoint ViewportSize = InViewport->GetSizeXY();

	FPlane Projection;
	if (InView == nullptr)
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		GetScene(),
		EngineShowFlags)
		.SetRealtimeUpdate(IsRealtime()));
		
		InView = CalcSceneView(&ViewFamily);
		Projection = InView->Project(InPosition);

		// InView will be deleted here
	}
	else
	{
		Projection = InView->Project(InPosition);
	}
	
	if (Projection.W > 0)
	{
		const float HighThreshold = 1.f - EdgePercentageLookAtThreshold;
		
		const int32 HalfX = 0.5f * Viewport->GetSizeXY().X;
		const int32 HalfY = 0.5f * Viewport->GetSizeXY().Y;
		const int32 XPos = HalfX + (HalfX * Projection.X);
		const int32 YPos = HalfY + (HalfY * (Projection.Y * -1));
			
		auto GetPercentToEdge = [&](int32 CurrentPos, int32 MaxPos) -> float
		{
			const float Center = static_cast<float>(MaxPos) / 2.f;
			const float RelativePosition = FMath::Abs(CurrentPos - Center);
			return RelativePosition / Center;
		};
			
		const float XPercent = GetPercentToEdge(XPos, ViewportSize.X);
		const float YPercent = GetPercentToEdge(YPos, ViewportSize.Y);
			
		if (OutPercentageToEdge)
		{
			*OutPercentageToEdge = FVector2D(XPercent, YPercent);
		}
			
		return XPercent >= HighThreshold || YPercent >= HighThreshold;
	}

	return false;
}

void FDisplayClusterLightCardEditorViewportClient::ResetFOVs()
{
	constexpr int32 MaxFOVs = 3;
	if (ProjectionFOVs.Num() < MaxFOVs)
	{
		ProjectionFOVs.AddDefaulted(MaxFOVs - ProjectionFOVs.Num());
	}
	ProjectionFOVs[static_cast<int32>(EDisplayClusterMeshProjectionType::Linear)] = 90.0f;
	ProjectionFOVs[static_cast<int32>(EDisplayClusterMeshProjectionType::Azimuthal)] = 130.0f;
	ProjectionFOVs[static_cast<int32>(EDisplayClusterMeshProjectionType::UV)] = 45.0f;
}

void FDisplayClusterLightCardEditorViewportClient::EnterDrawingLightCardMode()
{
	if (InputMode == EInputMode::Idle)
	{
		InputMode = EInputMode::DrawingLightCard;
		SelectLightCard(nullptr);
	}
}

void FDisplayClusterLightCardEditorViewportClient::ExitDrawingLightCardMode()
{
	if (InputMode == EInputMode::DrawingLightCard)
	{
		InputMode = EInputMode::Idle;
		DrawnMousePositions.Empty();
	}
}

bool FDisplayClusterLightCardEditorViewportClient::ShouldRenderPrimitive(const UPrimitiveComponent* PrimitiveComponent)
{
	const bool bIsUVProjection = ProjectionMode == EDisplayClusterMeshProjectionType::UV;
	if (ADisplayClusterLightCardActor* LightCard = Cast<ADisplayClusterLightCardActor>(PrimitiveComponent->GetOwner()))
	{
		// Only render the UV light cards when in UV projection mode, and only render non-UV light cards in any other projection mode
		return bIsUVProjection ? LightCard->bIsUVLightCard : !LightCard->bIsUVLightCard;
	}
	else
	{
		return true;
	}
}

bool FDisplayClusterLightCardEditorViewportClient::ShouldApplyProjectionToPrimitive(const UPrimitiveComponent* PrimitiveComponent)
{
	const bool bIsUVProjection = ProjectionMode == EDisplayClusterMeshProjectionType::UV;
	if (ADisplayClusterLightCardActor* LightCard = Cast<ADisplayClusterLightCardActor>(PrimitiveComponent->GetOwner()))
	{
		// When in UV projection mode, don't render the UV light cards using the UV projection, render them linearly
		if (bIsUVProjection && LightCard->bIsUVLightCard)
		{
			return false;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
