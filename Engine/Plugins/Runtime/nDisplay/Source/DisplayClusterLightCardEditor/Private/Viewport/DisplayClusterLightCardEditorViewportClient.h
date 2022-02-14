// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"

#include "DisplayClusterMeshProjectionRenderer.h"

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
	virtual FSceneView* CalcSceneView(FSceneViewFamily* ViewFamily, const int32 StereoViewIndex = INDEX_NONE) override;
	virtual bool IsLevelEditorClient() const override { return false; }
	virtual bool InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad) override;
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual void TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge) override;
	virtual void TrackingStopped() override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual ELevelViewportType GetViewportType() const override { return LVT_Perspective; }
	// ~FEditorViewportClient

	void SelectActor(AActor* NewActor);
	void ResetSelection();

	void UpdatePreviewActor(ADisplayClusterRootActor* RootActor);
	
	AActor* GetSelectedActor() const { return SelectedActor.Get(); }

	bool IsActorSelected() const { return SelectedActor.IsValid(); }
	
	EDisplayClusterMeshProjectionType GetProjectionMode() const { return ProjectionMode; }
	void SetProjectionMode(EDisplayClusterMeshProjectionType InProjectionMode);

	/** Gets the field of view of the specified projection mode */
	float GetProjectionModeFOV(EDisplayClusterMeshProjectionType InProjectionMode) const;

	/** Sets the field of view of the specified projection mode */
	void SetProjectionModeFOV(EDisplayClusterMeshProjectionType InProjectionMode, float NewFOV);

protected:
	/** Initiates a transaction. */
	void BeginTransaction(const FText& Description);

	/** Ends the current transaction, if one exists. */
	void EndTransaction();

	/** Gets a list of all primitive components to be rendered in the scene */
	void GetScenePrimitiveComponents(TArray<UPrimitiveComponent*>& OutPrimitiveComponents);

	/** Gets the scene view init options to use to create scene views for the preview scene */
	void GetSceneViewInitOptions(FSceneViewInitOptions& OutViewInitOptions);

	/** Finds a suitable primitive component on the stage actor to use as a projection origin */
	void FindProjectionOriginComponent();

private:
	TWeakPtr<FSceneViewport> SceneViewportPtr;
	TWeakPtr<SDisplayClusterLightCardEditor> LightCardEditorPtr;
	TWeakObjectPtr<ADisplayClusterRootActor> SpawnedRootActor;
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorLevelInstance;
	TWeakObjectPtr<AActor> SelectedActor;
	
	/** The renderer for the viewport, which can render the meshes with a variety of projection types */
	TSharedPtr<FDisplayClusterMeshProjectionRenderer> MeshProjectionRenderer;

	/** The current transaction for undo/redo */
	FScopedTransaction* ScopedTransaction = nullptr;
	
	bool bDraggingActor = false;

	/** The current projection mode the 3D viewport is being displayed with */
	EDisplayClusterMeshProjectionType ProjectionMode = EDisplayClusterMeshProjectionType::Perspective;

	/** The component of the root actor that is acting as the projection origin. Can be either the root component (stage origin) or a view origin component */
	TWeakObjectPtr<USceneComponent> ProjectionOriginComponent;

	/** Stores each projection mode's field of view separately */
	TArray<float> ProjectionFOVs;

	/** The increment to change the FOV by when using the scroll wheel */
	float FOVScrollIncrement = 5.0f;
};