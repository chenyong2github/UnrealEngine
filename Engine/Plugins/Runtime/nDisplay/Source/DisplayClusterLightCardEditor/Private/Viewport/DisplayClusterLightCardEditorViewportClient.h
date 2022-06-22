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

	struct FSphericalCoordinates
	{
	public:

		/** Constructors */
		FSphericalCoordinates(const FVector& CartesianPosition);
		FSphericalCoordinates();

		/** Return equivalent cartesian coordinates */
		FVector AsCartesian() const;

		/** Addition operator */
		FSphericalCoordinates operator+(FSphericalCoordinates const& Other) const;

		/** Subtraction operator */
		FSphericalCoordinates operator-(FSphericalCoordinates const& Other) const;

		/** Conform parameters to their normal ranges */
		void Conform();
		
		/** Returns a conformed version of this struct without changing the current one */
		FSphericalCoordinates GetConformed() const;

		/** Returns true if the inclination is pointing at north or south poles, within the given margin (in radians) */
		bool IsPointingAtPole(double Margin = 1e-6) const;

		double Radius = 0;      // unitless   0+      (when conforming)
		double Inclination = 0; // radians    0 to PI (when conforming)
		double Azimuth = 0;     // radians  -PI to PI (when conforming)
	};

	/** Custom render target that stores the normal data for the stage */
	class FNormalMap : public FRenderTarget
	{
	public:
		/** The size of the normal map */
		static const int32 NormalMapSize;

		/** The field of view to render the normal map with. Using the azimuthal projection,
		 * this is set so that the entire 360 degree scene is rendered */
		static const float NormalMapFOV;

		/** Initializes the normal map render target using the specified scene view options */
		void Init(const FSceneViewInitOptions& InSceneViewInitOptions);

		/** Releases the normal map render target's resources */
		void Release();

		/** Gets the size of the render target */
		virtual FIntPoint GetSizeXY() const override { return FIntPoint(SizeX, SizeY); }

		/** Gets a reference to the normal map data array, which stores the normal vector in the RGB components (color = 0.5 * Normal + 0.5) and the depth in the A component */
		TArray<FFloat16Color>& GetCachedNormalData() { return CachedNormalData; }

		/** Gets the normal vector and distance at the specified world location. The normal and distance are bilinearly interpolated from the nearest pixels in the normal map */
		bool GetNormalAndDistanceAtPosition(FVector Position, FVector& OutNormal, float& OutDistance) const;

		/** Morphs the vertices of the specified prodedural mesh to match the normal map */
		void MorphProceduralMesh(UProceduralMeshComponent* InProceduralMeshComponent) const;

		/** Generates a texture object that can be used to visualize the normal map */
		UTexture2D* GenerateNormalMapTexture(const FString& TextureName);

		/** Gets the normal map visualization texture, or null if it hasn't been generated */
		UTexture2D* GetNormalMapTexture() const { return NormalMapTexture.IsValid() ? NormalMapTexture.Get() : nullptr; }

	private:
		/** The view matrices used when the normal map was last rendered */
		FViewMatrices ViewMatrices;

		/** The cached normal map data from the last normal map render */
		TArray<FFloat16Color> CachedNormalData;

		/** A texture that contains the normal map, for visualization purposes */
		TWeakObjectPtr<UTexture2D> NormalMapTexture;

		/** The width of the normal map. */
		uint32 SizeX = 0;

		/** The height of the normal map. */
		uint32 SizeY = 0;
	};

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
	void MoveLightCardTo(ADisplayClusterLightCardActor& LightCard, const FSphericalCoordinates& SphericalCoords) const;

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
	
	/** Ensures that the light card root component is at the same location as the projection/view origin */
	void VerifyAndFixLightCardOrigin(ADisplayClusterLightCardActor* LightCard) const;

	/** Determines the appropriate delta rotation needed to move the specified light card to the mouse's location */
	FRotator GetLightCardRotationDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard, EAxisList::Type CurrentAxis);

	/** Determines the appropriate delta in spherical coordinates needed to move the specified light card to the mouse's location */
	FSphericalCoordinates GetLightCardTranslationDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard, EAxisList::Type CurrentAxis);

	/** Scales the currently selected light cards */
	void ScaleSelectedLightCards(FViewport* InViewport, EAxisList::Type CurrentAxis);

	/** Determines the appropriate scale delta needed to scale the light card */
	FVector2D GetLightCardScaleDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard, EAxisList::Type CurrentAxis);

	/** Rotates the currently selected light cards around the light card's normal axis */
	void SpinSelectedLightCards(FViewport* InViewport);

	/** Determines the appropriate spin delta needed to rotate the light card */
	double GetLightCardSpinDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard);

	/** Gets the spherical coordinates of the specified light card */
	FSphericalCoordinates GetLightCardCoordinates(ADisplayClusterLightCardActor* LightCard) const;

	/** Sets the light card position to the given spherical coordinates */
	void SetLightCardCoordinates(ADisplayClusterLightCardActor* LightCard, const FSphericalCoordinates& SphericalCoords) const;

	/** Performs a ray trace against the stage's geometry, and returns the hit point */
	bool TraceStage(const FVector& RayStart, const FVector& RayEnd, FVector& OutHitLocation) const;

	/** Traces the world geometry to find the best direction vector from the view origin to a valid point in space using a screen ray */
	FVector TraceScreenRay(const FVector& RayOrigin, const FVector& RayDirection, const FVector& ViewOrigin);

	/** Traces to find the light card corresponding to a click on a stage screen */
	ADisplayClusterLightCardActor* TraceScreenForLightCard(const FSceneView& View, int32 HitX, int32 HitY);

	/** Projects the specified world position to the viewport's current projection space */
	FVector ProjectWorldPosition(const FVector& UnprojectedWorldPosition, const FViewMatrices& ViewMatrices) const;

	/** Converts a pixel coordinate into a point and direction vector in world space */
	void PixelToWorld(const FSceneView& View, const FIntPoint& PixelPos, FVector& OutOrigin, FVector& OutDirection);

	/** Converts a world coordinate into a point in screen space, and returns true if the world position is on the screen */
	bool WorldToPixel(const FSceneView& View, const FVector& WorldPos, FVector2D& OutPixelPos) const;

	/** Converts a direction vector from world space to screen screen space, and returns true of the direction vector is on the screen */
	bool WorldToScreenDirection(const FSceneView& View, const FVector& WorldPos, const FVector& WorldDirection, FVector2D& OutScreenDir);

	/** Calculates the world transform to render the editor widget with to align it with the selected light card */
	bool CalcEditorWidgetTransform(FTransform& WidgetTransformBeforeMapProjection);
	
	/** Renders the viewport's normal map and stores the texture data to be used later */
	void RenderNormalMap(FNormalMap& NormalMap, const FVector& NormalMapDirection);

	/** Invalidates the viewport's normal map, forcing it to be rerendered on the next draw call */
	void InvalidateNormalMap();

	/** Checks if the location is approaching the edge of the view space */
	bool IsLocationCloseToEdge(const FVector& InPosition, const FViewport* InViewport = nullptr, const FSceneView* InView = nullptr, FVector2D* OutPercentageToEdge = nullptr);

	/** Resets the camera FOVs */
	void ResetFOVs();

	/** Creates a new light card using a polygon alpha mask as defined by the given mouse positions on the viewport */
	void CreateDrawnLightCard(const TArray<FIntPoint>& MousePositions);

	/** Calculates the final distance from the origin of a light card, given its flush distance and a desired offset */
	double CalculateFinalLightCardDistance(double FlushDistance, double DesiredOffsetFromFlush = 0.) const;

	/** Calculates the relative normal vector and world position in the specified direction from the given view origin */
	void CalculateNormalAndPositionInDirection(const FVector& InViewOrigin, const FVector& InDirection, FVector& OutWorldLocation, FVector& OutRelativeNormal, double InDesiredDistanceFromFlush = 0.) const;

private:
	TWeakPtr<FSceneViewport> SceneViewportPtr;
	TWeakPtr<SDisplayClusterLightCardEditorViewport> LightCardEditorViewportPtr;
	TWeakPtr<SDisplayClusterLightCardEditor> LightCardEditorPtr;
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorProxy;
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorLevelInstance;

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

	/** Light card preview actors being dropped on the scene */
	TArray<TWeakObjectPtr<ADisplayClusterLightCardActor>> DropPreviewLightCards;
	
	/** The index of the scene preview renderer returned from IDisplayClusterScenePreview */
	int32 PreviewRendererId = -1;
	
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

	/** The render target used to render a map of the screens' normals for the northern hemisphere of the view */
	FNormalMap NorthNormalMap;

	/** The render target used to render a map of the screens' normals for the southern hemisphere of the view */
	FNormalMap SouthNormalMap;

	/** A morphed ico-sphere mesh component that approximates the normal and depth map */
	TWeakObjectPtr<UProceduralMeshComponent> NormalMapMeshComponent;

	/** Indicates if the cached normal map is invalid and needs to be redrawn */
	bool bNormalMapInvalid = false;

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