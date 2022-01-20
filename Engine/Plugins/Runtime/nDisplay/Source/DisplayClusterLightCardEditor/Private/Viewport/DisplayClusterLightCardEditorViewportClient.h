// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"

class ADisplayClusterRootActor;
class SDisplayClusterLightCardEditor;
class FScopedTransaction;

/** Viewport Client for the preview viewport */
class FDisplayClusterLightCardEditorViewportClient : public FEditorViewportClient, public TSharedFromThis<FDisplayClusterLightCardEditorViewportClient>
{
public:
	FDisplayClusterLightCardEditorViewportClient(FAdvancedPreviewScene& InPreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget,
		TWeakPtr<SDisplayClusterLightCardEditor> InLightCardEditor);
	virtual ~FDisplayClusterLightCardEditorViewportClient() override;
	
	// FEditorViewportClient
	virtual FLinearColor GetBackgroundColor() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(FViewport* InViewport, FCanvas* Canvas) override;
	virtual bool CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const override { return true; }
	virtual bool CanCycleWidgetMode() const override { return true; }
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool IsLevelEditorClient() const override { return false; }
	virtual bool InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad) override;
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual void TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge) override;
	virtual void TrackingStopped() override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	// ~FEditorViewportClient

	void SetSceneViewport(TSharedPtr<FSceneViewport> InViewport);
	void SelectActor(AActor* NewActor);
	void ResetSelection();
	void ResetCamera();

	void UpdatePreviewActor(ADisplayClusterRootActor* RootActor);
	
	/**
	 * Returns true if the grid is currently visible in the viewport
	 */
	bool GetShowGrid() const;

	/**
	 * Will toggle the grid's visibility in the viewport
	 */
	void ToggleShowGrid();
	
	AActor* GetSelectedActor() const { return SelectedActor.Get(); }

	bool IsActorSelected() const { return SelectedActor.IsValid(); }
	
protected:
	/** Initiates a transaction. */
	void BeginTransaction(const FText& Description);

	/** Ends the current transaction, if one exists. */
	void EndTransaction();

private:
	TWeakPtr<FSceneViewport> SceneViewportPtr;
	TWeakPtr<SDisplayClusterLightCardEditor> LightCardEditorPtr;
	TWeakObjectPtr<ADisplayClusterRootActor> SpawnedRootActor;
	TWeakObjectPtr<AActor> SelectedActor;
	
	/** The full bounds of the preview scene (encompasses all visible components) */
	FBoxSphereBounds PreviewActorBounds;
	
	/** The current transaction for undo/redo */
	FScopedTransaction* ScopedTransaction;
	
	bool bDraggingActor;
};