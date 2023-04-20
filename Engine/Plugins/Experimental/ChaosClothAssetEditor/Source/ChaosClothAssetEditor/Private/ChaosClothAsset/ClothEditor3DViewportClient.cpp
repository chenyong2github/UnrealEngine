// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditor3DViewportClient.h"
#include "ChaosClothAsset/ClothEditorMode.h"
#include "ChaosClothAsset/ClothEditorToolkit.h"
#include "EditorModeManager.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowActor.h"
#include "Dataflow/DataflowRenderingFactory.h"
#include "EdModeInteractiveToolsContext.h"
#include "BaseGizmos/TransformProxy.h"
#include "BaseGizmos/CombinedTransformGizmo.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Animation/SkeletalMeshActor.h"
#include "AssetViewerSettings.h"
#include "Editor/EditorPerProjectUserSettings.h"

FChaosClothAssetEditor3DViewportClient::FChaosClothAssetEditor3DViewportClient(FEditorModeTools* InModeTools,
	TSharedPtr<FChaosClothPreviewScene> InPreviewScene,
	const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(InModeTools, InPreviewScene.Get(), InEditorViewportWidget),
	  ClothPreviewScene(InPreviewScene)
{
	// We want our near clip plane to be quite close so that we can zoom in further.
	OverrideNearClipPlane(KINDA_SMALL_NUMBER);

	// Call this once with the default value to get everything in a consistent state
	EnableRenderMeshWireframe(bRenderMeshWireframe);

	EngineShowFlags.SetSelectionOutline(true);

	// Set up Gizmo and TransformProxy

	UModeManagerInteractiveToolsContext* const InteractiveToolsContext = ModeTools->GetInteractiveToolsContext();
	TransformProxy = NewObject<UTransformProxy>();

	UInteractiveGizmoManager* const GizmoManager = InteractiveToolsContext->GizmoManager;
	const FString GizmoIdentifier = TEXT("ChaosClothAssetEditor3DViewportClientGizmoIdentifier");
	Gizmo = GizmoManager->Create3AxisTransformGizmo(this, GizmoIdentifier);

	Gizmo->SetActiveTarget(TransformProxy);
	Gizmo->SetVisibility(false);
	Gizmo->bUseContextGizmoMode = false;
	Gizmo->bUseContextCoordinateSystem = false;
	Gizmo->ActiveGizmoMode = EToolContextTransformGizmoMode::Combined;

	// Set correct flags according to current profile settings
	SetAdvancedShowFlagsForScene(UAssetViewerSettings::Get()->Profiles[GetMutableDefault<UEditorPerProjectUserSettings>()->AssetViewerProfileIndex].bPostProcessingEnabled);
}

void FChaosClothAssetEditor3DViewportClient::RegisterDelegates()
{
	// Remove any existing delegate in case this function is called twice
	UAssetViewerSettings::Get()->OnAssetViewerSettingsChanged().RemoveAll(this);
	UAssetViewerSettings::Get()->OnAssetViewerSettingsChanged().AddSP(this, &FChaosClothAssetEditor3DViewportClient::OnAssetViewerSettingsChanged);

	USelection* const SelectedComponents = ModeTools->GetSelectedComponents();
	SelectedComponents->SelectionChangedEvent.RemoveAll(this);
	SelectedComponents->SelectionChangedEvent.AddSP(this, &FChaosClothAssetEditor3DViewportClient::ComponentSelectionChanged);
}

FChaosClothAssetEditor3DViewportClient::~FChaosClothAssetEditor3DViewportClient()
{
	DeleteViewportGizmo();

	UAssetViewerSettings::Get()->OnAssetViewerSettingsChanged().RemoveAll(this);
	if (USelection* const SelectedComponents = ModeTools->GetSelectedComponents())
	{
		SelectedComponents->SelectionChangedEvent.RemoveAll(this);
	}
}

void FChaosClothAssetEditor3DViewportClient::DeleteViewportGizmo()
{
	if (Gizmo && ModeTools && ModeTools->GetInteractiveToolsContext() && ModeTools->GetInteractiveToolsContext()->GizmoManager)
	{
		ModeTools->GetInteractiveToolsContext()->GizmoManager->DestroyGizmo(Gizmo);
	}
	Gizmo = nullptr;
	TransformProxy = nullptr;
}

void FChaosClothAssetEditor3DViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEditorViewportClient::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(TransformProxy);
	Collector.AddReferencedObject(Gizmo);
}

void FChaosClothAssetEditor3DViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	auto GetLatestTimestamp = [](const UDataflow* Dataflow, const Dataflow::FContext* Context) -> Dataflow::FTimestamp
	{
		if (Dataflow && Context)
		{
			return FMath::Max(Dataflow->GetRenderingTimestamp().Value, Context->GetTimestamp().Value);
		}
		return Dataflow::FTimestamp::Invalid;
	};

	if (TSharedPtr<const FChaosClothAssetEditorToolkit> PinnedClothToolkit = ClothToolkit.Pin())
	{
		if (const TSharedPtr<Dataflow::FContext> Context = PinnedClothToolkit->GetDataflowContext())
		{
			if (const UDataflow* const Dataflow = PinnedClothToolkit->GetDataflow())
			{
				if (UDataflowComponent* const DataflowComponent = ClothEdMode->GetDataflowComponent())
				{
					const Dataflow::FTimestamp SystemTimestamp = GetLatestTimestamp(Dataflow, Context.Get());

					if (SystemTimestamp >= LastModifiedTimestamp)
					{
						if (Dataflow->GetRenderTargets().Num())
						{
							// Component Object Rendering
							DataflowComponent->ResetRenderTargets();
							DataflowComponent->SetDataflow(Dataflow);
							DataflowComponent->SetContext(Context);
							for (const UDataflowEdNode* const Node : Dataflow->GetRenderTargets())
							{
								DataflowComponent->AddRenderTarget(Node);
							}
						}
						else
						{
							DataflowComponent->ResetRenderTargets();
						}

						LastModifiedTimestamp = GetLatestTimestamp(Dataflow, Context.Get()).Value + 1;
					}
				}
			}
		}
	}

	// Note: we don't tick the PreviewWorld here, that is done in UChaosClothAssetEditorMode::ModeTick()

}

void FChaosClothAssetEditor3DViewportClient::EnableRenderMeshWireframe(bool bEnable)
{
	bRenderMeshWireframe = bEnable;

	if (ClothComponent)
	{
		ClothComponent->SetForceWireframe(bRenderMeshWireframe);
	}
}

void FChaosClothAssetEditor3DViewportClient::SetClothComponent(TObjectPtr<UChaosClothComponent> InClothComponent)
{
	ClearSelectedComponents();
	ClothComponent = InClothComponent;
}

void FChaosClothAssetEditor3DViewportClient::SetClothEdMode(TObjectPtr<UChaosClothAssetEditorMode> InClothEdMode)
{
	ClothEdMode = InClothEdMode;
}

void FChaosClothAssetEditor3DViewportClient::SetClothEditorToolkit(TWeakPtr<const FChaosClothAssetEditorToolkit> InClothToolkit)
{
	ClothToolkit = InClothToolkit;
}

void FChaosClothAssetEditor3DViewportClient::SoftResetSimulation()
{
	if (ClothEdMode)
	{
		ClothEdMode->SoftResetSimulation();
	}
}

void FChaosClothAssetEditor3DViewportClient::HardResetSimulation()
{
	if (ClothEdMode)
	{
		ClothEdMode->HardResetSimulation();
	}
}

void FChaosClothAssetEditor3DViewportClient::SuspendSimulation()
{
	if (ClothEdMode)
	{
		ClothEdMode->SuspendSimulation();
	}
}

void FChaosClothAssetEditor3DViewportClient::ResumeSimulation()
{
	if (ClothEdMode)
	{
		ClothEdMode->ResumeSimulation();
	}
}

bool FChaosClothAssetEditor3DViewportClient::IsSimulationSuspended() const
{
	if (ClothEdMode)
	{
		return ClothEdMode->IsSimulationSuspended();
	}

	return false;
}

void FChaosClothAssetEditor3DViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
}

FBox FChaosClothAssetEditor3DViewportClient::PreviewBoundingBox() const
{
	if (ClothEdMode)
	{
		return ClothEdMode->PreviewBoundingBox();
	}

	return FBox(ForceInitToZero);
}

TWeakPtr<FChaosClothPreviewScene> FChaosClothAssetEditor3DViewportClient::GetClothPreviewScene()
{
	return ClothPreviewScene;
}

TWeakPtr<const FChaosClothPreviewScene> FChaosClothAssetEditor3DViewportClient::GetClothPreviewScene() const
{
	return ClothPreviewScene;
}

void FChaosClothAssetEditor3DViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

	const bool bIsShiftKeyDown = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
	const bool bIsCtrlKeyDown = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);

	USelection* SelectedComponents = ModeTools->GetSelectedComponents();

	TArray<UPrimitiveComponent*> PreviouslySelectedComponents;
	SelectedComponents->GetSelectedObjects<UPrimitiveComponent>(PreviouslySelectedComponents);

	SelectedComponents->Modify();
	SelectedComponents->BeginBatchSelectOperation();

	if (!bIsShiftKeyDown && !bIsCtrlKeyDown)
	{
		SelectedComponents->DeselectAll();
	}

	if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		const HActor* const ActorProxy = static_cast<HActor*>(HitProxy);
		if (ActorProxy && ActorProxy->Actor)
		{
			const AActor* const Actor = ActorProxy->Actor;		
			USceneComponent* RootComponent = Actor->GetRootComponent();
			if (RootComponent)
			{
				if (bIsShiftKeyDown)
				{
					SelectedComponents->Select(RootComponent);
				}
				else if (bIsCtrlKeyDown)
				{
					// Don't use USelection::ToggleSelect here because that checks membership in GObjectSelection, not the given USelection...
					TArray<USceneComponent*> Components;
					SelectedComponents->GetSelectedObjects(Components);
					const bool bIsSelected = Components.Contains(RootComponent);	

					if (bIsSelected)
					{
						SelectedComponents->Deselect(RootComponent);
					}
					else
					{
						SelectedComponents->Select(RootComponent);
					}
				}
				else
				{
					SelectedComponents->Select(RootComponent);
				}

				if (UPrimitiveComponent* const PrimitiveComponent = Cast<UPrimitiveComponent>(RootComponent))
				{
					PrimitiveComponent->PushSelectionToProxy();
				}
			}
		}
	}

	SelectedComponents->EndBatchSelectOperation();

	for (UPrimitiveComponent* const Component : PreviouslySelectedComponents)
	{
		Component->PushSelectionToProxy();
	}
}


void FChaosClothAssetEditor3DViewportClient::ClearSelectedComponents()
{
	USelection* const SelectedComponents = ModeTools->GetSelectedComponents();
	TArray<UPrimitiveComponent*> PreviouslySelectedComponents;
	SelectedComponents->GetSelectedObjects<UPrimitiveComponent>(PreviouslySelectedComponents);

	for (UPrimitiveComponent* const Component : PreviouslySelectedComponents)
	{
		Component->PushSelectionToProxy();
	}

	SelectedComponents->DeselectAll();
}

void FChaosClothAssetEditor3DViewportClient::ComponentSelectionChanged(UObject* NewSelection)
{
	USelection* const SelectedComponents = ModeTools->GetSelectedComponents();

	// Update TransformProxy

	TransformProxy = NewObject<UTransformProxy>();
	TArray<USceneComponent*> Components;
	SelectedComponents->GetSelectedObjects(Components);
	for (USceneComponent* SelectedComponent : Components)
	{
		TransformProxy->AddComponent(SelectedComponent);
	}

	// Update gizmo
	if (Gizmo)
	{
		if (Components.Num() > 0)
		{
			Gizmo->SetActiveTarget(TransformProxy);
			Gizmo->SetVisibility(true);
		}
		else
		{
			Gizmo->ClearActiveTarget();
			Gizmo->SetVisibility(false);
		}
	}
}


void FChaosClothAssetEditor3DViewportClient::OnAssetViewerSettingsChanged(const FName& InPropertyName)
{
	if (InPropertyName == GET_MEMBER_NAME_CHECKED(FPreviewSceneProfile, bPostProcessingEnabled) || InPropertyName == NAME_None)
	{
		const UAssetViewerSettings* const Settings = UAssetViewerSettings::Get();
		const TSharedPtr<const FChaosClothPreviewScene> PinnedClothPreviewScene = ClothPreviewScene.Pin();
		if (Settings && PinnedClothPreviewScene)
		{
			const int32 ProfileIndex = PinnedClothPreviewScene->GetCurrentProfileIndex();
			if (Settings->Profiles.IsValidIndex(ProfileIndex))
			{
				SetAdvancedShowFlagsForScene(Settings->Profiles[ProfileIndex].bPostProcessingEnabled);
			}
		}
	}
}

void FChaosClothAssetEditor3DViewportClient::SetAdvancedShowFlagsForScene(const bool bAdvancedShowFlags)
{
	if (bAdvancedShowFlags)
	{
		EngineShowFlags.EnableAdvancedFeatures();
	}
	else
	{
		EngineShowFlags.DisableAdvancedFeatures();
	}
}