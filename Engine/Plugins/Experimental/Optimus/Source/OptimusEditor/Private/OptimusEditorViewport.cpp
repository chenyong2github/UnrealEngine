// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorViewport.h"

#include "OptimusEditor.h"

#include "OptimusDeformer.h"
#include "DataInterfaces/DataInterfaceSkeletalMeshRead.h"
#include "DataInterfaces/DataInterfaceSkinCacheWrite.h"

#include "ComponentAssetBroker.h"
#include "ComputeFramework/ComputeGraphComponent.h"
#include "EditorViewportClient.h"
#include "Editor/UnrealEdEngine.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "UnrealEdGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "UnrealWidget.h"

class FOptimusEditorViewportClient : public FEditorViewportClient
{
public:
	FOptimusEditorViewportClient(TWeakPtr<IOptimusEditor> InEditor, FAdvancedPreviewScene& InPreviewScene, const TSharedRef<SOptimusEditorViewport>& InEditorViewport);

	// FEditorViewportClient overrides
	bool InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed = 1.f, bool bGamepad = false) override;
	bool InputAxis(FViewport* InViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples/* =1 */, bool bGamepad/* =false */) override;
	FLinearColor GetBackgroundColor() const override;
	void Tick(float DeltaSeconds) override;
	void Draw(FViewport* Viewport, FCanvas* Canvas) override;
	bool ShouldOrbitCamera() const override;

	/**
	* Focuses the viewport to the center of the bounding box/sphere ensuring that the entire bounds are in view
	*
	* @param Bounds   The bounds to focus
	* @param bInstant Whether or not to focus the viewport instantly or over time
	*/
	void FocusViewportOnBounds(const FBoxSphereBounds Bounds, bool bInstant = false);

private:

	/** Pointer back to the editor tool that owns us */
	TWeakPtr<IOptimusEditor> EditorOwner;

	TWeakPtr<SOptimusEditorViewport> EditorViewport;

	/** Preview Scene - uses advanced preview settings */
	FAdvancedPreviewScene* AdvancedPreviewScene;
};

FOptimusEditorViewportClient::FOptimusEditorViewportClient(
	TWeakPtr<IOptimusEditor> InEditor, 
	FAdvancedPreviewScene& InPreviewScene, 
	const TSharedRef<SOptimusEditorViewport>& InEditorViewport 
	)
	: FEditorViewportClient(nullptr, &InPreviewScene, StaticCastSharedRef<SEditorViewport>(InEditorViewport))
	, EditorOwner(InEditor)
{
	// Setup defaults for the common draw helper.
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = false;
	DrawHelper.GridColorAxis = FColor(80, 80, 80);
	DrawHelper.GridColorMajor = FColor(72, 72, 72);
	DrawHelper.GridColorMinor = FColor(64, 64, 64);
	DrawHelper.PerspectiveGridSize = HALF_WORLD_MAX1;

	SetViewMode(VMI_Lit);

	EngineShowFlags.DisableAdvancedFeatures();
	EngineShowFlags.SetSnap(0);
	EngineShowFlags.SetSeparateTranslucency(true);

	OverrideNearClipPlane(1.0f);
	bUsingOrbitCamera = true;

	// Don't want to display the widget in this viewport
	Widget->SetDefaultVisibility(false);

	EditorViewport = InEditorViewport;
	
	AdvancedPreviewScene = &InPreviewScene;
}



void FOptimusEditorViewportClient::Tick(float DeltaSeconds)
{
	TSharedPtr<SOptimusEditorViewport> EditorViewportPtr = EditorViewport.Pin();
	if (EditorViewport.IsValid())
	{
		EditorViewportPtr->GetComputeGraphComponent()->QueueExecute();
	}
	
	FEditorViewportClient::Tick(DeltaSeconds);

	// Tick the preview scene world.
	if (!GIntraFrameDebuggingGameThread)
	{
		PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
}


void FOptimusEditorViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	FEditorViewportClient::Draw(InViewport, Canvas);

	// FIXME
	// MaterialEditorPtr.Pin()->DrawMessages(InViewport, Canvas);
}

bool FOptimusEditorViewportClient::ShouldOrbitCamera() const
{
	// Should always orbit around the preview object to keep it in view.
	return true;
}

bool FOptimusEditorViewportClient::InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad)
{
	bool bHandled = FEditorViewportClient::InputKey(InViewport, ControllerId, Key, Event, AmountDepressed, false);

	// Handle viewport screenshot.
	bHandled |= InputTakeScreenshot(InViewport, Key, Event);

	bHandled |= AdvancedPreviewScene->HandleInputKey(InViewport, ControllerId, Key, Event, AmountDepressed, bGamepad);

	return bHandled;
}

bool FOptimusEditorViewportClient::InputAxis(FViewport* InViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples/* =1 */, bool bGamepad/* =false */)
{
	bool bResult = true;

	if (!bDisableInput)
	{
		bResult = AdvancedPreviewScene->HandleViewportInput(InViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
		if (bResult)
		{
			Invalidate();
		}
		else
		{
			bResult = FEditorViewportClient::InputAxis(InViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
		}
	}

	return bResult;
}

FLinearColor FOptimusEditorViewportClient::GetBackgroundColor() const
{
	if (AdvancedPreviewScene != nullptr)
	{
		return AdvancedPreviewScene->GetBackgroundColor();
	}
	else
	{
		FLinearColor BackgroundColor = FLinearColor::Black;
		// FIXME
#if 0
		if (MaterialEditorPtr.IsValid())
		{
			UMaterialInterface* MaterialInterface = MaterialEditorPtr.Pin()->GetMaterialInterface();
			if (MaterialInterface)
			{
				const EBlendMode PreviewBlendMode = (EBlendMode)MaterialInterface->GetBlendMode();
				if (PreviewBlendMode == BLEND_Modulate)
				{
					BackgroundColor = FLinearColor::White;
				}
				else if (PreviewBlendMode == BLEND_Translucent || PreviewBlendMode == BLEND_AlphaComposite || PreviewBlendMode == BLEND_AlphaHoldout)
				{
					BackgroundColor = FColor(64, 64, 64);
				}
			}
		}
#endif
		return BackgroundColor;
	}
}

void FOptimusEditorViewportClient::FocusViewportOnBounds(const FBoxSphereBounds Bounds, bool bInstant /*= false*/)
{
	const FVector Position = Bounds.Origin;
	float Radius = Bounds.SphereRadius;

	float AspectToUse = AspectRatio;
	FIntPoint ViewportSize = Viewport->GetSizeXY();
	if (!bUseControllingActorViewInfo && ViewportSize.X > 0 && ViewportSize.Y > 0)
	{
		AspectToUse = Viewport->GetDesiredAspectRatio();
	}

	const bool bEnable = false;
	ToggleOrbitCamera(bEnable);

	/**
	* We need to make sure we are fitting the sphere into the viewport completely, so if the height of the viewport is less
	* than the width of the viewport, we scale the radius by the aspect ratio in order to compensate for the fact that we have
	* less visible vertically than horizontally.
	*/
	if (AspectToUse > 1.0f)
	{
		Radius *= AspectToUse;
	}

	/**
	* Now that we have a adjusted radius, we are taking half of the viewport's FOV,
	* converting it to radians, and then figuring out the camera's distance from the center
	* of the bounding sphere using some simple trig.  Once we have the distance, we back up
	* along the camera's forward vector from the center of the sphere, and set our new view location.
	*/
	const float HalfFOVRadians = FMath::DegreesToRadians(ViewFOV / 2.0f);
	const float DistanceFromSphere = Radius / FMath::Sin(HalfFOVRadians);
	FViewportCameraTransform& ViewTransform = GetViewTransform();
	FVector CameraOffsetVector = ViewTransform.GetRotation().Vector() * -DistanceFromSphere;

	ViewTransform.SetLookAt(Position);
	ViewTransform.TransitionToLocation(Position + CameraOffsetVector, EditorViewportWidget, bInstant);

	// Tell the viewport to redraw itself.
	Invalidate();
}



void SOptimusEditorViewport::Construct(const FArguments& InArgs, TWeakPtr<FOptimusEditor> InEditor)
{
	EditorOwner = InEditor;
	AdvancedPreviewScene = MakeShareable(new FAdvancedPreviewScene(FPreviewScene::ConstructionValues()));

	SEditorViewport::Construct(SEditorViewport::FArguments());
	
	// Create the compute graph component.
	ComputeGraphComponent = NewObject<UComputeGraphComponent>(GetTransientPackage(), UComputeGraphComponent::StaticClass(), NAME_None, RF_Transient);
	if (TSharedPtr<FOptimusEditor> Editor = InEditor.Pin(); Editor.IsValid())
	{
		ComputeGraphComponent->ComputeGraph = Editor->GetDeformer();

		// Set up the data interfaces. Those will get filled in when we set the preview asset.
		SkeletalMeshReadDataProvider = NewObject<USkeletalMeshReadDataProvider>(GetTransientPackage(), USkeletalMeshReadDataProvider::StaticClass(), NAME_None, RF_Transient);
		ComputeGraphComponent->DataProviders.Add(SkeletalMeshReadDataProvider);
		SkeletalMeshSkinCacheDataProvider = NewObject<USkeletalMeshSkinCacheDataProvider>(GetTransientPackage(), USkeletalMeshSkinCacheDataProvider::StaticClass(), NAME_None, RF_Transient);
		ComputeGraphComponent->DataProviders.Add(SkeletalMeshSkinCacheDataProvider);
	}

	SetPreviewAsset(GUnrealEd->GetThumbnailManager()->EditorSphere);
}


SOptimusEditorViewport::~SOptimusEditorViewport()
{
	
}


bool SOptimusEditorViewport::SetPreviewAsset(UObject* InAsset)
{
	// Unregister the current component
	if (PreviewMeshComponent != nullptr)
	{
		AdvancedPreviewScene->RemoveComponent(PreviewMeshComponent);
		AdvancedPreviewScene->RemoveComponent(ComputeGraphComponent);
		
		PreviewMeshComponent->MarkPendingKill();
		PreviewMeshComponent = nullptr;
	}

	if (TSubclassOf<UActorComponent> ComponentClass = FComponentAssetBrokerage::GetPrimaryComponentForAsset(InAsset->GetClass()))
	{
		if (ComponentClass->IsChildOf(UMeshComponent::StaticClass()))
		{
			PreviewMeshComponent = NewObject<UMeshComponent>(GetTransientPackage(), ComponentClass, NAME_None, RF_Transient);

			FComponentAssetBrokerage::AssignAssetToComponent(PreviewMeshComponent, InAsset);
			
			if (USkeletalMeshComponent *SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PreviewMeshComponent))
			{
				SkeletalMeshReadDataProvider->SkeletalMesh = SkeletalMeshComponent;
				SkeletalMeshSkinCacheDataProvider->SkeletalMesh = SkeletalMeshComponent;
			}
		}
	}

	if (PreviewMeshComponent != nullptr)
	{
		if (GEditor->PreviewPlatform.GetEffectivePreviewFeatureLevel() <= ERHIFeatureLevel::ES3_1)
		{
			PreviewMeshComponent->SetMobility(EComponentMobility::Static);
		}
		AdvancedPreviewScene->AddComponent(PreviewMeshComponent, FTransform::Identity);
		AdvancedPreviewScene->SetFloorOffset(-PreviewMeshComponent->Bounds.Origin.Z + PreviewMeshComponent->Bounds.BoxExtent.Z);

		// The compute graph component must currently come after the skelmesh component because
		// it writes over data that the skincache creates.
		AdvancedPreviewScene->AddComponent(ComputeGraphComponent, FTransform::Identity);
	}

	// Make sure the preview material is applied to the component
	// SetPreviewMaterial(PreviewMaterial);

	return (PreviewMeshComponent != nullptr);
}


void SOptimusEditorViewport::SetOwnerTab(const TSharedRef<SDockTab>& InOwnerTab)
{
	OwnerTab = InOwnerTab;
}


void SOptimusEditorViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ComputeGraphComponent);
	Collector.AddReferencedObject(SkeletalMeshReadDataProvider);
	Collector.AddReferencedObject(SkeletalMeshSkinCacheDataProvider);
	Collector.AddReferencedObject(PreviewMeshComponent);
	Collector.AddReferencedObject(PreviewMaterial);
}


TSharedRef<class SEditorViewport> SOptimusEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}


TSharedPtr<FExtender> SOptimusEditorViewport::GetExtenders() const
{
	return MakeShareable(new FExtender);
}


TSharedRef<FEditorViewportClient> SOptimusEditorViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable(new FOptimusEditorViewportClient(EditorOwner, *AdvancedPreviewScene.Get(), SharedThis(this)));
	// UAssetViewerSettings::Get()->OnAssetViewerSettingsChanged().AddRaw(this, &SOptimusEditorViewport::OnAssetViewerSettingsChanged);
	EditorViewportClient->SetViewLocation(FVector::ZeroVector);
	EditorViewportClient->SetViewRotation(FRotator(-15.0f, -90.0f, 0.0f));
	EditorViewportClient->SetViewLocationForOrbiting(FVector::ZeroVector);
	EditorViewportClient->bSetListenerPosition = false;
	EditorViewportClient->EngineShowFlags.EnableAdvancedFeatures();
	EditorViewportClient->EngineShowFlags.SetLighting(true);
	EditorViewportClient->EngineShowFlags.SetIndirectLightingCache(true);
	EditorViewportClient->EngineShowFlags.SetPostProcessing(true);
	EditorViewportClient->Invalidate();
	EditorViewportClient->VisibilityDelegate.BindSP(this, &SOptimusEditorViewport::IsVisible);

	return EditorViewportClient.ToSharedRef();
}


bool SOptimusEditorViewport::IsVisible() const
{
	// We're not visible if the owning tab is not visible either.
	return ViewportWidget.IsValid() && (!OwnerTab.IsValid() || OwnerTab.Pin()->IsForeground()) && SEditorViewport::IsVisible();
}
