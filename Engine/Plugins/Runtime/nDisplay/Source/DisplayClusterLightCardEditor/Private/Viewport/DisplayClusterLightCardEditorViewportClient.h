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
				Inclination = 0;
			}

			Azimuth = FMath::Atan2(CartesianPosition.Y, CartesianPosition.X);
		}

		FSphericalCoordinates()
			: Radius(0)
			, Inclination(0)
			, Azimuth(0)
		{ }

		FVector AsCartesian() const
		{
			const double SinAzimuth = FMath::Sin(Azimuth);
			const double CosAzimuth = FMath::Cos(Azimuth);

			const double SinInclination = FMath::Sin(Inclination);
			const double CosInclination = FMath::Cos(Inclination);

			return FVector(
				Radius * CosAzimuth * SinInclination,
				Radius * SinAzimuth * SinInclination,
				Radius * CosInclination
			);
		}

		double Radius = 0;
		double Inclination = 0;
		double Azimuth = 0;
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

	/** The amount to offset a light card's DistanceFromCenter when making it flush with a screen */
	static constexpr float LightCardFlushOffset = -0.5f;

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

	/** Resets the camera to the initial rotation / position */
	void ResetCamera(bool bLocationOnly = false);

	/** Moves specified card to desired coordinates 
	 *
	 * @param LightCard The light card that we are moving
	 * @param SphericalCoords specifies desired location of light card in spherical coordinates with respect to view origin
	*/
	void MoveLightCardTo(ADisplayClusterLightCardActor& LightCard, const FSphericalCoordinates& SphericalCoords) const;

	/** Places the given light card in the middle of the current viewport */
	void CenterLightCardInView(ADisplayClusterLightCardActor& LightCard);
	
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

	/** Ensures that the light card root component is at the same location as the projection/view origin */
	void VerifyAndFixLightCardOrigin(ADisplayClusterLightCardActor* LightCard) const;

	/** Determines the appropriate delta in spherical coordinates needed to move the specified light card to the mouse's location */
	FSphericalCoordinates GetLightCardTranslationDelta(FViewport* InViewport, ADisplayClusterLightCardActor* LightCard, EAxisList::Type CurrentAxis);

	/** Gets the spherical coordinates of the specified light card */
	FSphericalCoordinates GetLightCardCoordinates(ADisplayClusterLightCardActor* LightCard) const;

	/** Traces to find the light card corresponding to a click on a stage screen */
	ADisplayClusterLightCardActor* TraceScreenForLightCard(const FSceneView& View, int32 HitX, int32 HitY);

	/** Converts a pixel coordinate into a point and direction vector in world space */
	void PixelToWorld(const FSceneView& View, const FIntPoint& PixelPos, FVector& OutOrigin, FVector& OutDirection);

	/** Calculates the world transform to render the editor widget with to align it with the selected light card */
	bool CalcEditorWidgetTransform(FTransform& WidgetTransformBeforeMapProjection, FTransform& WidgetTransformAfterMapProjection);
	
	/** Renders the viewport's normal map and stores the texture data to be used later */
	void RenderNormalMap(FNormalMap& NormalMap, const FVector& NormalMapDirection);

	/** Invalidates the viewport's normal map, forcing it to be rerendered on the next draw call */
	void InvalidateNormalMap();

	/** Checks if the location is approaching the edge of the view space */
	bool IsLocationCloseToEdge(const FVector& InPosition, const FViewport* InViewport = nullptr, const FSceneView* InView = nullptr, FVector2D* OutPercentageToEdge = nullptr);

	/** Resets the camera FOVs */
	void ResetFOVs();

private:
	TWeakPtr<FSceneViewport> SceneViewportPtr;
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
	
	/** The renderer for the viewport, which can render the meshes with a variety of projection types */
	TSharedPtr<FDisplayClusterMeshProjectionRenderer> MeshProjectionRenderer;
	
	/** Indicates that the user is dragging an actor in the viewport */
	bool bDraggingActor = false;

	/** The LC editor widget used to manipulate light cards */
	TSharedPtr<FDisplayClusterLightCardEditorWidget> EditorWidget;

	/** The cached editor widget transform after map projection calculated for the editor widget on the last tick */
	FTransform CachedEditorWidgetTransformAfterMapProjection;

	/** The cached editor widget transform before map projection calculated for the editor widget on the last tick */
	FTransform CachedEditorWidgetTransformBeforeMapProjection;

	/** The offset between the widget's origin and the place it was clicked when a drag action was started */
	FVector DragWidgetOffset;

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

	/** Indicates if the cached normal map is invalid and needs to be redrawn */
	bool bNormalMapInvalid = false;

	/** Indicates if the normal map should be displayed to the screen */
	bool bDisplayNormalMapVisualization = false;
};