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

FChaosClothAssetEditor3DViewportClient::FChaosClothAssetEditor3DViewportClient(FEditorModeTools* InModeTools,
	FPreviewScene* InPreviewScene, 
	const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(InModeTools, InPreviewScene, InEditorViewportWidget)
{
	// We want our near clip plane to be quite close so that we can zoom in further.
	OverrideNearClipPlane(KINDA_SMALL_NUMBER);

	// Call this once with the default value to get everything in a consistent state
	EnableRenderMeshWireframe(bRenderMeshWireframe);
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

	if (ClothToolkit.IsValid())
	{
		if (const TSharedPtr<Dataflow::FContext> Context = ClothToolkit->GetDataflowContext())
		{
			if (const UDataflow* const Dataflow = ClothToolkit->GetDataflow())
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
	ClothComponent = InClothComponent;
}

void FChaosClothAssetEditor3DViewportClient::SetClothEdMode(TObjectPtr<UChaosClothAssetEditorMode> InClothEdMode)
{
	ClothEdMode = InClothEdMode;
}

void FChaosClothAssetEditor3DViewportClient::SetClothEditorToolkit(TSharedPtr<const FChaosClothAssetEditorToolkit> InClothToolkit)
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
	if (!bSimMeshWireframe)
	{
		return;
	}

	// TODO: Draw simulation mesh wireframe

}

FBox FChaosClothAssetEditor3DViewportClient::PreviewBoundingBox() const
{
	if (ClothEdMode)
	{
		return ClothEdMode->PreviewBoundingBox();
	}

	return FBox(ForceInitToZero);
}