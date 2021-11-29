// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MLDeformerAsset.h"
#include "IPersonaEditMode.h"
#include "MLDeformerEditorData.h"

class FMLDeformerEditorToolkit;
class FMLDeformerPreviewScene;
class FMLDeformerEditorData;
class UMLDeformerVizSettings;
struct FMLDeformerEditorActor;

class FMLDeformerEditorMode : public IPersonaEditMode
{
public:
	static FName ModeName;

	FMLDeformerEditorMode();

	void SetEditorData(TSharedPtr<FMLDeformerEditorData> InEditorData);

	/** IPersonaEditMode interface. */
	virtual bool GetCameraTarget(FSphere& OutTarget) const override;
	virtual class IPersonaPreviewScene& GetAnimPreviewScene() const override;
	virtual void GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const override;

	/** FEdMode interface. */
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override { return true; }
	virtual bool AllowWidgetMove() override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;

private:
	void UpdateActors();
	void UpdateLabels();
	void EncapsulateBounds(const FMLDeformerEditorActor* Actor, FBox& Box) const;
	void DrawDebugPoints(FPrimitiveDrawInterface* PDI, const TArray<FVector3f>& Points, int32 DepthGroup, const FLinearColor& Color);

private:
	TWeakPtr<FMLDeformerEditorData> EditorData;
};
