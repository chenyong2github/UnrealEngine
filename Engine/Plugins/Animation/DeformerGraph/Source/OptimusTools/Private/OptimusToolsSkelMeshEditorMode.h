// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

#include "OptimusToolsSkelMeshEditorMode.generated.h"


class FStylusStateTracker;
class FOptimusToolsSkelMeshEditorModeToolkit;
class UEdModeInteractiveToolsContext;


UCLASS()
class UOptimusToolsSkelMeshEditorMode : 
	public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()
public:
	const static FEditorModeID Id;	

	UOptimusToolsSkelMeshEditorMode();
	UOptimusToolsSkelMeshEditorMode(FVTableHelper& Helper);

	~UOptimusToolsSkelMeshEditorMode();

	// UEdMode overrides
	void Initialize() override;

	void Enter() override;
	void Exit() override;
	void CreateToolkit() override;

	void Tick(FEditorViewportClient* InViewportClient, float InDeltaTime) override;
	// void Render(const FSceneView* InView, FViewport* InViewport, FPrimitiveDrawInterface* InPDI) override;
	// void DrawHUD(FEditorViewportClient* InViewportClient, FViewport* InViewport, const FSceneView* InView, FCanvas* InCanvas) override;

	bool UsesToolkits() const override { return true; }

private:
	TUniquePtr<FStylusStateTracker> StylusStateTracker;
};
