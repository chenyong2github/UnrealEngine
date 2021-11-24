// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorViewportClient.h"
#include "InputBehaviorSet.h"
#include "UVEditor2DViewportBehaviorTargets.h" // FUVEditor2DScrollBehaviorTarget, FUVEditor2DMouseWheelZoomBehaviorTarget
#include "UVToolContextObjects.h" // UUVToolViewportButtonsAPI::ESelectionMode

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

	bool AreSelectionButtonsEnabled() const;
	void SetSelectionMode(UUVToolViewportButtonsAPI::ESelectionMode NewMode);
	UUVToolViewportButtonsAPI::ESelectionMode GetSelectionMode() const;

	bool AreWidgetButtonsEnabled() const;

	// FEditorViewportClient
	virtual bool InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float/*AmountDepressed*/, bool/*Gamepad*/) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool ShouldOrbitCamera() const override;
	bool CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const override;
	void SetWidgetMode(UE::Widget::EWidgetMode NewMode) override;
	UE::Widget::EWidgetMode GetWidgetMode() const override;
	// Overriding base class visibility
	using FEditorViewportClient::OverrideNearClipPlane;

	// IInputBehaviorSource
	virtual const UInputBehaviorSet* GetInputBehaviors() const override;

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

protected:
	// These get added in AddReferencedObjects for memory management
	UInputBehaviorSet* BehaviorSet;
	UUVToolViewportButtonsAPI* ViewportButtonsAPI;

	// Position to place the camera far plane relative to world z
	float CameraFarPlaneWorldZ = -10.0;
	// The near plane gets positioned some proportion to z = 0. We don't use a constant value because our depth offset values are percentage-based
	// Lower proportions move the plane nearer to world z
	float CameraNearPlaneProportionZ = 0.8;

	// Note that it's generally less hassle if the unique ptr types are complete here,
	// not forward declared, else we get compile errors if their destruction shows up
	// anywhere in the header.
	TUniquePtr<FUVEditor2DScrollBehaviorTarget> ScrollBehaviorTarget;
	TUniquePtr<FUVEditor2DMouseWheelZoomBehaviorTarget> ZoomBehaviorTarget;
};