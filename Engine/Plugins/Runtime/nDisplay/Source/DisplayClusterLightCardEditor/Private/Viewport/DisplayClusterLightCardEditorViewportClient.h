// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"

#include "DisplayClusterLightCardEditorProxyType.h"
#include "DisplayClusterMeshProjectionRenderer.h"
#include "DisplayClusterLightCardActor.h"

class ADisplayClusterRootActor;
class SDisplayClusterLightCardEditor;
class FScopedTransaction;
class FDisplayClusterLightCardEditorWidget;
class UDisplayClusterConfigurationViewport;

/** Viewport Client for the preview viewport */
class FDisplayClusterLightCardEditorViewportClient : public FEditorViewportClient, public TSharedFromThis<FDisplayClusterLightCardEditorViewportClient>
{
private:

	struct FSphericalCoordinates
	{
	public:
		FSphericalCoordinates(const FVector& CartesianPosition)
		{
			Radius = CartesianPosition.Size();

			if (Radius > UE_SMALL_NUMBER)
			{
				Inclination = FMath::Acos(CartesianPosition.Z / Radius);
			}
			else
			{
				Inclination = 0.f;
			}

			Azimuth = FMath::Atan2(CartesianPosition.Y, CartesianPosition.X);
		}

		FSphericalCoordinates()
			: Radius(0.f)
			, Inclination(0.f)
			, Azimuth(0.f)
		{ }

		float Radius = 0.f;
		float Inclination = 0.f;
		float Azimuth = 0.f;
	};

public:
	FDisplayClusterLightCardEditorViewportClient(FAdvancedPreviewScene& InPreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget,
		TWeakPtr<SDisplayClusterLightCardEditor> InLightCardEditor);
	virtual ~FDisplayClusterLightCardEditorViewportClient() override;
	
	// FEditorViewportClient
	virtual FLinearColor GetBackgroundColor() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(FViewport* InViewport, FCanvas* Canvas) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const override { return false; }
	virtual bool CanCycleWidgetMode() const override { return false; }
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override { return UE::Widget::WM_None; }
	virtual FSceneView* CalcSceneView(FSceneViewFamily* ViewFamily, const int32 StereoViewIndex = INDEX_NONE) override;
	virtual bool IsLevelEditorClient() const override { return false; }
	virtual bool InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad) override;
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual void TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge) override;
	virtual void TrackingStopped() override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual EMouseCursor::Type GetCursor(FViewport* Viewport,int32 X,int32 Y) override;
	virtual ELevelViewportType GetViewportType() const override { return LVT_Perspective; }
	// ~FEditorViewportClient

	/**
	 * Update the spawned preview actor from a root actor in the level
	 * 
	 * @param RootActor The new root actor to use. Accepts nullptr
	 * @param bForce Force the update even if the RootActor hasn't changed
	 * @param ProxyType The proxy type to destroy and update
	 */
	void UpdatePreviewActor(ADisplayClusterRootActor* RootActor, bool bForce = false,
		EDisplayClusterLightCardEditorProxyType ProxyType = EDisplayClusterLightCardEditorProxyType::All);

	/** Only update required transform values of proxies */
	void UpdateProxyTransforms();

	/** Remove proxies of the specified type */
	void DestroyProxies(EDisplayClusterLightCardEditorProxyType ProxyType);
	
	/** Selects the light card proxies that correspond to the specified light cards */
	void SelectLightCards(const TArray<AActor*>& LightCardsToSelect);

	EDisplayClusterMeshProjectionType GetProjectionMode() const { return ProjectionMode; }
	void SetProjectionMode(EDisplayClusterMeshProjectionType InProjectionMode);

	/** Gets the field of view of the specified projection mode */
	float GetProjectionModeFOV(EDisplayClusterMeshProjectionType InProjectionMode) const;

	/** Sets the field of view of the specified projection mode */
	void SetProjectionModeFOV(EDisplayClusterMeshProjectionType InProjectionMode, float NewFOV);

private:
	/** Initiates a transaction. */
	void BeginTransaction(const FText& Description);

	/** Ends the current transaction, if one exists. */
	void EndTransaction();
	
	/** Gets a list of all primitive components to be rendered in the scene */
	void GetScenePrimitiveComponents(TArray<UPrimitiveComponent*>& OutPrimitiveComponents);

	/** Gets the scene view init options to use to create scene views for the preview scene */
	void GetSceneViewInitOptions(FSceneViewInitOptions& OutViewInitOptions);

	/** Gets the viewport that is attached to the specified primitive component */
	UDisplayClusterConfigurationViewport* FindViewportForPrimitiveComponent(UPrimitiveComponent* PrimitiveComponent);

	/** Finds a suitable primitive component on the stage actor to use as a projection origin */
	void FindProjectionOriginComponent();

	/** Gets a list of all light card actors on the level linked to the specified root actor */
	void FindLightCardsForRootActor(ADisplayClusterRootActor* RootActor, TArray<TWeakObjectPtr<ADisplayClusterLightCardActor>>& OutLightCards);

	/** Callback to check if an light card actor is among the list of selected light card actors */
	bool IsLightCardSelected(const AActor* Actor);

	/** Adds the specified light card actor the the list of selected light cards */
	void SelectLightCard(ADisplayClusterLightCardActor* Actor, bool bAddToSelection = false);

	/** Notifies the light card editor of the currently selected light cards so that it may update other UI components to match */
	void PropagateLightCardSelection();

	/** Propagates the specified light card proxy's transform back to its level instance version */
	void PropagateLightCardTransform(ADisplayClusterLightCardActor* LightCardProxy);

	/** Moves the currently selected light cards */
	void MoveSelectedLightCards(FViewport* InViewport, EAxisList::Type CurrentAxis);

	/** Determines the appropriate delta in spherical coordinates needed to move the specified light card to the mouse's location */
	FSphericalCoordinates GetLightCardTranslationDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard, EAxisList::Type CurrentAxis);

	/** Gets the spherical coordinates of the specified light card */
	FSphericalCoordinates GetLightCardCoordinates(ADisplayClusterLightCardActor* LightCard) const;

	/** Traces to find the light card corresponding to a click on a stage screen */
	ADisplayClusterLightCardActor* TraceScreenForLightCard(const FSceneView& View, int32 HitX, int32 HitY);

	/** Converts a pixel coordinate into a point and direction vector in world space */
	void PixelToWorld(const FSceneView& View, const FIntPoint& PixelPos, FVector& OutOrigin, FVector& OutDirection);

	/** Calculates the world transform to render the editor widget with to align it with the selected light card */
	FTransform CalcEditorWidgetTransform();

private:
	TWeakPtr<FSceneViewport> SceneViewportPtr;
	TWeakPtr<SDisplayClusterLightCardEditor> LightCardEditorPtr;
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorProxy;
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorLevelInstance;

	struct FLightCardProxy
	{
		TWeakObjectPtr<ADisplayClusterLightCardActor> LevelInstance;
		TWeakObjectPtr<ADisplayClusterLightCardActor> Proxy;

		FLightCardProxy(ADisplayClusterLightCardActor* InLevelInstance, ADisplayClusterLightCardActor* InProxy)
			: LevelInstance(InLevelInstance)
			, Proxy(InProxy)
		{ }

		bool operator==(AActor* Actor) const { return (LevelInstance.IsValid() && LevelInstance.Get() == Actor) || (Proxy.IsValid() && Proxy.Get() == Actor); }
	};

	TArray<FLightCardProxy> LightCardProxies;
	TArray<TWeakObjectPtr<ADisplayClusterLightCardActor>> SelectedLightCards;
	
	/** The renderer for the viewport, which can render the meshes with a variety of projection types */
	TSharedPtr<FDisplayClusterMeshProjectionRenderer> MeshProjectionRenderer;
	
	/** Indicates that the user is dragging an actor in the viewport */
	bool bDraggingActor = false;

	/** The LC editor widget used to manipulate light cards */
	TSharedPtr<FDisplayClusterLightCardEditorWidget> EditorWidget;

	/** The cached editor widget transform calculated for the editor widget on the last tick */
	FTransform CachedEditorWidgetTransform;

	/** The offset between the widget's origin and the place it was clicked when a drag action was started */
	FVector DragWidgetOffset;

	/** The current projection mode the 3D viewport is being displayed with */
	EDisplayClusterMeshProjectionType ProjectionMode = EDisplayClusterMeshProjectionType::Perspective;

	/** The component of the root actor that is acting as the projection origin. Can be either the root component (stage origin) or a view origin component */
	TWeakObjectPtr<USceneComponent> ProjectionOriginComponent;

	/** Stores each projection mode's field of view separately */
	TArray<float> ProjectionFOVs;

	/** The increment to change the FOV by when using the scroll wheel */
	float FOVScrollIncrement = 5.0f;
};