// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"

#include "DisplayClusterLightCardEditorProxyType.h"
#include "DisplayClusterMeshProjectionRenderer.h"
#include "DisplayClusterLightCardActor.h"
#include "DisplayClusterLightCardEditorWidget.h"
#include "DisplayClusterLightCardEditorHelper.h"

class ADisplayClusterRootActor;
class SDisplayClusterLightCardEditor;
class SDisplayClusterLightCardEditorViewport;
class FScopedTransaction;
class UDisplayClusterConfigurationViewport;
class UProceduralMeshComponent;

/** Viewport Client for the preview viewport */
class FDisplayClusterLightCardEditorViewportClient : public FEditorViewportClient, public TSharedFromThis<FDisplayClusterLightCardEditorViewportClient>
{
	using Super = FEditorViewportClient;

public:

	/** State machine that helps keep track of the context to which user inputs apply (e.g. mouse buttons, key presses) */
	enum class EInputMode
	{
		Idle,

		/** Indicates that the user is dragging an actor in the viewport */
		DraggingActor,

		/** Indicates that the user is drawing a light card in the viewport */
		DrawingLightCard,
	};

private:
	DECLARE_MULTICAST_DELEGATE(FOnNextSceneRefresh);

public:
	FDisplayClusterLightCardEditorViewportClient(FPreviewScene& InPreviewScene,
		const TWeakPtr<SDisplayClusterLightCardEditorViewport>& InEditorViewportWidget);
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
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual void TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge) override;
	virtual void TrackingStopped() override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual EMouseCursor::Type GetCursor(FViewport* Viewport,int32 X,int32 Y) override;
	virtual ELevelViewportType GetViewportType() const override { return LVT_Perspective; }
	virtual bool HasDropPreviewActors() const override { return DropPreviewLightCards.Num() > 0; }
	virtual void DestroyDropPreviewActors() override;
	virtual bool UpdateDropPreviewActors(int32 MouseX, int32 MouseY, const TArray<UObject*>& DroppedObjects, bool& bOutDroppedObjectsVisible, UActorFactory* FactoryToUse) override;
	virtual bool DropObjectsAtCoordinates(int32 MouseX, int32 MouseY, const TArray<UObject*>& DroppedObjects, TArray<AActor*>& OutNewActors, bool bOnlyDropOnTarget, bool bCreateDropPreview, bool bSelectActors, UActorFactory* FactoryToUse) override;
	// ~FEditorViewportClient

	/** Returns a delegate that is invoked on the next scene refresh. The delegate is cleared afterwards */
	FOnNextSceneRefresh& GetOnNextSceneRefresh() { return OnNextSceneRefreshDelegate; }

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
	void SelectLightCards(const TArray<ADisplayClusterLightCardActor*>& LightCardsToSelect);

	FDisplayClusterLightCardEditorWidget::EWidgetMode GetEditorWidgetMode() const { return EditorWidget->GetWidgetMode(); }
	void SetEditorWidgetMode(FDisplayClusterLightCardEditorWidget::EWidgetMode InWidgetMode) { EditorWidget->SetWidgetMode(InWidgetMode); }

	/** Gets the current projection mode of the editor viewport */
	EDisplayClusterMeshProjectionType GetProjectionMode() const { return ProjectionMode; }

	/** Gets the current viewport type the viewport is being rendered with */
	ELevelViewportType GetRenderViewportType() const { return RenderViewportType; }

	/** Sets the projection mode and the render viewport type of the viewport */
	void SetProjectionMode(EDisplayClusterMeshProjectionType InProjectionMode, ELevelViewportType InViewportType);

	/** Gets the field of view of the specified projection mode */
	float GetProjectionModeFOV(EDisplayClusterMeshProjectionType InProjectionMode) const;

	/** Sets the field of view of the specified projection mode */
	void SetProjectionModeFOV(EDisplayClusterMeshProjectionType InProjectionMode, float NewFOV);

	/** Resets the camera to the initial rotation / position */
	void ResetCamera(bool bLocationOnly = false);

	/** Moves specified card to desired coordinates. Actual radius will be based on flush constraint and LightCard's RadialOffset.
	 *
	 * @param LightCard The light card that we are moving
	 * @param SphericalCoords specifies desired location of light card in spherical coordinates with respect to view origin.
	 * 
	*/
	void MoveLightCardTo(ADisplayClusterLightCardActor& LightCard, const FDisplayClusterLightCardEditorHelper::FSphericalCoordinates& SphericalCoords) const;

	/** Places the given light card in the middle of the current viewport */
	void CenterLightCardInView(ADisplayClusterLightCardActor& LightCard);

	/** Moves all selected light cards to the specified pixel position */
	void MoveSelectedLightCardsToPixel(const FIntPoint& PixelPos);

	/** Returns the current input mode */
	EInputMode GetInputMode() { return InputMode; }

	/** Requests that we enter light card drawing input mode */
	void EnterDrawingLightCardMode();

	/** Requests that we exit light card drawing input mode (and go back to idle/normal) */
	void ExitDrawingLightCardMode();

private:
	/** Initiates a transaction. */
	void BeginTransaction(const FText& Description);

	/** Ends the current transaction, if one exists. */
	void EndTransaction();
	
	/** Gets a list of all primitive components to be rendered in the scene */
	void GetScenePrimitiveComponents(TArray<UPrimitiveComponent*>& OutPrimitiveComponents);

	/** Gets the scene view init options to use to create scene views for the preview scene */
	void GetSceneViewInitOptions(FSceneViewInitOptions& OutViewInitOptions);

	/** Gets the scene view init options to use when rendering the normal map cache */
	void GetNormalMapSceneViewInitOptions(FIntPoint NormalMapSize, float NormalMapFOV, const FVector& ViewDirection, FSceneViewInitOptions& OutViewInitOptions);

	/** Gets the viewport that is attached to the specified primitive component */
	UDisplayClusterConfigurationViewport* FindViewportForPrimitiveComponent(UPrimitiveComponent* PrimitiveComponent);

	/** Finds a suitable primitive component on the stage actor to use as a projection origin */
	USceneComponent* FindProjectionOriginComponent(const ADisplayClusterRootActor* InRootActor) const;

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

	/** Moves all given light cards to the specified pixel position */
	void MoveLightCardsToPixel(const FIntPoint& PixelPos, const TArray<TWeakObjectPtr<ADisplayClusterLightCardActor>>& InLightCards);

	/** Moves the currently selected UV light cards */
	void MoveSelectedUVLightCards(FViewport* InViewport, EAxisList::Type CurrentAxis);

	/** Determines the appropriate delta in UV coordinates needed to move the specified light card to the mouse's location */
	FVector2D GetUVLightCardTranslationDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard, EAxisList::Type CurrentAxis);

	/** Scales the currently selected light cards */
	void ScaleSelectedLightCards(FViewport* InViewport, EAxisList::Type CurrentAxis);

	/** Determines the appropriate scale delta needed to scale the light card */
	FVector2D GetLightCardScaleDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard, EAxisList::Type CurrentAxis);

	/** Rotates the currently selected light cards around the light card's normal axis */
	void SpinSelectedLightCards(FViewport* InViewport);

	/** Determines the appropriate spin delta needed to rotate the light card */
	double GetLightCardSpinDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard);

	/** Traces to find the light card corresponding to a click on a stage screen */
	ADisplayClusterLightCardActor* TraceScreenForLightCard(const FSceneView& View, int32 HitX, int32 HitY);

	/** Projects the specified world position to the viewport's current projection space */
	FVector ProjectWorldPosition(const FVector& UnprojectedWorldPosition, const FViewMatrices& ViewMatrices) const;

	/** Converts a direction vector from world space to screen screen space, and returns true of the direction vector is on the screen */
	bool WorldToScreenDirection(const FSceneView& View, const FVector& WorldPos, const FVector& WorldDirection, FVector2D& OutScreenDir);

	/** Calculates the world transform to render the editor widget with to align it with the selected light card */
	bool CalcEditorWidgetTransform(FTransform& WidgetTransform);

	/** Checks if the location is approaching the edge of the view space */
	bool IsLocationCloseToEdge(const FVector& InPosition, const FViewport* InViewport = nullptr, const FSceneView* InView = nullptr, FVector2D* OutPercentageToEdge = nullptr);

	/** Resets the camera FOVs */
	void ResetFOVs();

	/** Creates a new light card using a polygon alpha mask as defined by the given mouse positions on the viewport */
	void CreateDrawnLightCard(const TArray<FIntPoint>& MousePositions);

	/** Calculates the final distance from the origin of a light card, given its flush distance and a desired offset */
	double CalculateFinalLightCardDistance(double FlushDistance, double DesiredOffsetFromFlush = 0.) const;

	/** Callback passed into the mesh projection renderer to filter which primitives are drawn */
	bool ShouldRenderPrimitive(const UPrimitiveComponent* PrimitiveComponent);

	/** Callback passed into the mesh projection renderer to filter which primitives drawn with the current projection and which are drawn linearly */
	bool ShouldApplyProjectionToPrimitive(const UPrimitiveComponent* PrimitiveComponent);

private:
	TWeakPtr<FSceneViewport> SceneViewportPtr;
	TWeakPtr<SDisplayClusterLightCardEditorViewport> LightCardEditorViewportPtr;
	TWeakPtr<SDisplayClusterLightCardEditor> LightCardEditorPtr;
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorProxy;
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorLevelInstance;

	/** Proxy types that are currently refreshing over 1-2 frames */
	TSet<EDisplayClusterLightCardEditorProxyType> ProxyTypesRefreshing;

	/** Handle to world timer when refreshing */
	FTimerHandle RefreshTimerHandle;
	
	/** The radius of the bounding sphere that entirely encapsulates the root actor */
	float RootActorBoundingRadius = 0.0f;

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

	/** The light card in the selected light card list that was selected last */
	TWeakObjectPtr<ADisplayClusterLightCardActor> LastSelectedLightCard = nullptr;

	/** Light card preview actors being dropped on the scene */
	TArray<TWeakObjectPtr<ADisplayClusterLightCardActor>> DropPreviewLightCards;
	
	/** The index of the scene preview renderer returned from IDisplayClusterScenePreview */
	int32 PreviewRendererId = -1;

	/** Helper used to convert between screen and world coordinates for lightcard positions. */
	TSharedPtr<FDisplayClusterLightCardEditorHelper> ProjectionHelper;
	
	/** The LC editor widget used to manipulate light cards */
	TSharedPtr<FDisplayClusterLightCardEditorWidget> EditorWidget;

	/** The cached editor widget transform in unprojected world space */
	FTransform CachedEditorWidgetWorldTransform;

	/** The offset between the widget's origin and the place it was clicked when a drag action was started */
	FVector DragWidgetOffset;

	/** The mouse position registered on the previous widget input pass */
	FIntPoint LastWidgetMousePos;

	/** The location the camera should be looking at */
	TOptional<FVector> DesiredLookAtLocation;

	/** The current speed to use when looking at a location */
	float DesiredLookAtSpeed = 1.f;

	/** The maximum speed to use when looking at a location */
	float MaxDesiredLookAtSpeed = 5.f;

	/** The percentage to the edge of the view which should trigger auto tilt and auto pan */
	float EdgePercentageLookAtThreshold = 0.1f;

	/** The current projection mode the 3D viewport is being displayed with */
	EDisplayClusterMeshProjectionType ProjectionMode = EDisplayClusterMeshProjectionType::Azimuthal;

	/** The viewport type (perspective or orthogonal) to use when rendering the viewport. Separate from ViewportType since ViewportType also determines input functionality */
	ELevelViewportType RenderViewportType = LVT_Perspective;

	/** The component of the root actor that is acting as the projection origin. Can be either the root component (stage origin) or a view origin component */
	TWeakObjectPtr<USceneComponent> ProjectionOriginComponent;

	/** Stores each projection mode's field of view separately */
	TArray<float> ProjectionFOVs;

	/** The increment to change the FOV by when using the scroll wheel */
	float FOVScrollIncrement = 5.0f;

	/** Indicates if the normal map should be displayed to the screen */
	bool bDisplayNormalMapVisualization = false;

	/** Current input mode */
	EInputMode InputMode = EInputMode::Idle;

	/** Array of mouse positions that will be used to spawn a new light card with the shape of the drawn polygon */
	TArray<FIntPoint> DrawnMousePositions;

	/** Multicast delegate that stores callbacks to be invoked the next time the scene is refreshed */
	FOnNextSceneRefresh OnNextSceneRefreshDelegate;

	/** A flag that disables drawing with the custom projection renderer and instead renders the viewport client with the editor's default renderer */
	bool bDisableCustomRenderer = false;
};