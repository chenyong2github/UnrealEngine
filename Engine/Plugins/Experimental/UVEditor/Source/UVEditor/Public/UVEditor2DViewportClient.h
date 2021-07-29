// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorViewportClient.h"
#include "InputBehaviorSet.h"
#include "UVEditor2DViewportBehaviorTargets.h" // FUVEditor2DScrollBehaviorTarget, FUVEditor2DMouseWheelZoomBehaviorTarget

//class UDensityAdjustingGrid;

/**
 * Client used to display a 2D view of the UV's, implemented by using a perspective viewport with a locked
 * camera.
 */
class UVEDITOR_API FUVEditor2DViewportClient : public FEditorViewportClient, public IInputBehaviorSource
{
public:
	FUVEditor2DViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene = nullptr,
		const TWeakPtr<SEditorViewport>& InEditorViewportWidget = nullptr);

	virtual ~FUVEditor2DViewportClient() {}

	// FEditorViewportClient
	virtual bool InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float/*AmountDepressed*/, bool/*Gamepad*/) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool ShouldOrbitCamera() const override;

	// IInputBehaviorSource
	virtual const UInputBehaviorSet* GetInputBehaviors() const override;

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

protected:
	// These get added in AddReferencedObjects for memory management
	UInputBehaviorSet* BehaviorSet;
	//UDensityAdjustingGrid* Grid;

	// Note that it's generally less hassle if the unique ptr types are complete here,
	// not forward declared, else we get compile errors if their destruction shows up
	// anywhere in the header.
	TUniquePtr<FUVEditor2DScrollBehaviorTarget> ScrollBehaviorTarget;
	TUniquePtr<FUVEditor2DMouseWheelZoomBehaviorTarget> ZoomBehaviorTarget;
};