// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorViewportClient.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "GeometryCollection/ManagedArrayCollection.h"

class UChaosClothComponent;
class UChaosClothAssetEditorMode;
class FChaosClothAssetEditorToolkit;

/**
 * Viewport client for the 3d sim preview in the cloth editor. Currently same as editor viewport
 * client but doesn't allow editor gizmos/widgets.
 */
class CHAOSCLOTHASSETEDITOR_API FChaosClothAssetEditor3DViewportClient : public FEditorViewportClient
{
public:

	FChaosClothAssetEditor3DViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene = nullptr,
		const TWeakPtr<SEditorViewport>& InEditorViewportWidget = nullptr);

	virtual ~FChaosClothAssetEditor3DViewportClient() = default;

	virtual bool CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const override {	return false; }
	virtual void SetWidgetMode(UE::Widget::EWidgetMode NewMode) override {}
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override { return UE::Widget::EWidgetMode::WM_None; }

	virtual void Tick(float DeltaSeconds) override;

	void EnableSimMeshWireframe(bool bEnable ) { bSimMeshWireframe = bEnable; }
	bool SimMeshWireframeEnabled() const { return bSimMeshWireframe; }

	void EnableRenderMeshWireframe(bool bEnable);
	bool RenderMeshWireframeEnabled() const { return bRenderMeshWireframe; }

	void SetClothComponent(TObjectPtr<UChaosClothComponent> ClothComponent);
	void SetClothEdMode(TObjectPtr<UChaosClothAssetEditorMode> ClothEdMode);
	void SetClothEditorToolkit(TSharedPtr<const FChaosClothAssetEditorToolkit> ClothToolkit);

	void SoftResetSimulation();
	void HardResetSimulation();
	void SuspendSimulation();
	void ResumeSimulation();
	bool IsSimulationSuspended() const;

	FBox PreviewBoundingBox() const;

private:

	TObjectPtr<UChaosClothComponent> ClothComponent;

	TObjectPtr<UChaosClothAssetEditorMode> ClothEdMode;

	TSharedPtr<const FChaosClothAssetEditorToolkit> ClothToolkit;

	// Debug draw of simulation meshes
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	
	bool bSimMeshWireframe = true;
	bool bRenderMeshWireframe = false;

	//~ Dataflow render support

	Dataflow::FTimestamp LastModifiedTimestamp = Dataflow::FTimestamp::Invalid;

	//~ Dataflow render support
};
