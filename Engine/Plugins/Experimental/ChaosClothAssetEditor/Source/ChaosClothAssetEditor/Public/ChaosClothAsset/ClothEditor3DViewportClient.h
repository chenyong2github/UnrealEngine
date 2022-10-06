// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class USkeletalMeshComponent;
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

	void EnableSimMeshWireframe(bool bEnable ) { bSimMeshWireframe = bEnable; }
	bool SimMeshWireframeEnabled() const { return bSimMeshWireframe; }

	void EnableRenderMeshWireframe(bool bEnable);
	bool RenderMeshWireframeEnabled() const { return bRenderMeshWireframe; }

	void SetSkeletalMeshComponents(const TArray<TObjectPtr<USkeletalMeshComponent>>& NewSkMeshComponents)
	{
		SkeletalMeshComponents = NewSkMeshComponents;
	}

protected:

	TArray<TObjectPtr<USkeletalMeshComponent>> SkeletalMeshComponents;

	// Debug draw of simulation meshes
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;

	bool bSimMeshWireframe = true;
	bool bRenderMeshWireframe = false;
};
