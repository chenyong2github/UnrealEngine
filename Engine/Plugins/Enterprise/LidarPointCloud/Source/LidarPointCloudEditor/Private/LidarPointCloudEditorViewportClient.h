// Copyright Epic Games, Inc. All Rights Reserved.
 
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UnrealWidget.h"
#include "EditorViewportClient.h"
#include "Components.h"

class FAdvancedPreviewScene;
class FCanvas;
class FLidarPointCloudEditor;
class SLidarPointCloudEditorViewport;
class ULidarPointCloud;
class ULidarPointCloudComponent;

enum class ELidarPointCloudSelectionMode : uint8
{
	None,
	Add,
	Subtract,
	Replace
};

/** Viewport Client for the preview viewport */
class FLidarPointCloudEditorViewportClient : public FEditorViewportClient, public TSharedFromThis<FLidarPointCloudEditorViewportClient>
{
public:
	FLidarPointCloudEditorViewportClient(TWeakPtr<FLidarPointCloudEditor> InPointCloudEditor, const TSharedRef<SLidarPointCloudEditorViewport>& InPointCloudEditorViewport, FAdvancedPreviewScene* InPreviewScene, ULidarPointCloud* InPreviewPointCloud, ULidarPointCloudComponent* InPreviewPointCloudComponent);
	~FLidarPointCloudEditorViewportClient();

	// FEditorViewportClient interface
	virtual void MouseMove(FViewport* Viewport, int32 x, int32 y) override;
	virtual bool InputKey(FViewport* Viewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed = 1.f, bool bGamepad = false) override;
	virtual bool InputAxis(FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples = 1, bool bGamepad = false) override;
	virtual void ProcessClick(class FSceneView& View, class HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override { return false; }
	virtual void TrackingStarted(const struct FInputEventState& InInputState, bool bIsDragging, bool bNudge) override {}
	virtual void TrackingStopped() override {}
	virtual FWidget::EWidgetMode GetWidgetMode() const override { return FWidget::WM_None; }
	virtual void SetWidgetMode(FWidget::EWidgetMode NewMode) override {}
	virtual bool CanSetWidgetMode(FWidget::EWidgetMode NewMode) const override { return false; }
	virtual bool CanCycleWidgetMode() const override { return false; }
	virtual FVector GetWidgetLocation() const override { return FVector::ZeroVector; }
	virtual FMatrix GetWidgetCoordSystem() const override { return FMatrix::Identity; }
	virtual ECoordSystem GetWidgetCoordSystemSpace() const override { return COORD_Local; }
	virtual bool ShouldOrbitCamera() const override;

	void ResetCamera();

	/** Callback for toggling the nodes show flag. */
	void ToggleShowNodes();

	/** Callback for checking the nodes show flag. */
	bool IsSetShowNodesChecked() const;

protected:
	// FEditorViewportClient interface
	virtual void PerspectiveCameraMoved() override;

	/** Call back for when the user changes preview scene settings in the UI */
	void OnAssetViewerSettingsChanged(const FName& InPropertyName);
	/** Used to (re)-set the viewport show flags related to post processing*/
	void SetAdvancedShowFlagsForScene(const bool bAdvancedShowFlags);

private:
	void OnSelectionStart();
	void OnSelectionEnd(const FIntVector4& SelectionArea, ELidarPointCloudSelectionMode Mode);

private:
	/** Component for the point cloud. */
	TWeakObjectPtr<ULidarPointCloudComponent> PointCloudComponent;

	/** Pointer back to the PointCloud editor tool that owns us */
	TWeakPtr<FLidarPointCloudEditor> PointCloudEditorPtr;

	/** Pointer back to the PointCloudEditor viewport control that owns us */
	TWeakPtr<SLidarPointCloudEditorViewport> PointCloudEditorViewportPtr;

	/** Stored pointer to the preview scene in which the point cloud is shown */
	FAdvancedPreviewScene* AdvancedPreviewScene;

private:
	ELidarPointCloudSelectionMode SelectionMode;

	/** Location of the mouse cursor when the selection has started */
	FIntPoint SelectionStartLocation;
};
