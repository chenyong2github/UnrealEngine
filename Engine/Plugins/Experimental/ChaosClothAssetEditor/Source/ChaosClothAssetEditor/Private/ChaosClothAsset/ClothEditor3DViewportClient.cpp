// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditor3DViewportClient.h"
#include "ChaosClothAsset/ClothEditorMode.h"
#include "EditorModeManager.h"
#include "ChaosClothAsset/ClothComponent.h"

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