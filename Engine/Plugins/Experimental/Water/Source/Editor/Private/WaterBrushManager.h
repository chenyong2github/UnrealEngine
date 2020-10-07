// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WaterLandscapeBrush.h"
#include "WaterBrushCacheContainer.h"
#include "WaterBodyHeightmapSettings.h"
#include "WaterBodyWeightmapSettings.h"
#include "WaterCurveSettings.h"
#include "WaterBodyActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "WaterBrushManager.generated.h"


class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UJumpFloodComponent2D;
class UMaterialInstanceDynamic;
class UTexture2D;
class IWaterBrushActorInterface;
class UCanvas;
class AWaterBodyIsland;
class UCurveFloat;
class UCurveBase;
class UMaterialParameterCollection;


UCLASS(config = Engine, Blueprintable, BlueprintType)
class AWaterBrushManager : public AWaterLandscapeBrush
{
public:
	GENERATED_BODY()
	
 	UPROPERTY(VisibleAnywhere, meta=(Category="Default"))
	USceneCaptureComponent2D* SceneCaptureComponent2D = nullptr;

	UPROPERTY(VisibleAnywhere, meta=(Category="Default"))
	UJumpFloodComponent2D* JumpFloodComponent2D = nullptr;

	// RTs
	UPROPERTY(VisibleAnywhere, Transient, meta = (DisplayName = "Heightmap RT A", Category = "Render Targets", OverrideNativeName = "Heightmap RT A"))
	UTextureRenderTarget2D* HeightmapRTA = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="Heightmap RT B", Category="Render Targets", OverrideNativeName="Heightmap RT B"))
	UTextureRenderTarget2D* HeightmapRTB = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="Jump Flood RT A", Category="Render Targets", OverrideNativeName="Jump Flood RT A"))
	UTextureRenderTarget2D* JumpFloodRTA;

	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="Jump Flood RT B", Category="Render Targets", OverrideNativeName="Jump Flood RT B"))
	UTextureRenderTarget2D* JumpFloodRTB = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="Depth And Shape RT A", Category="Render Targets", OverrideNativeName="Depth and Shape RT A"))
	UTextureRenderTarget2D* DepthAndShapeRT = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="Water Depth And Velocity RT", Category="Render Targets", OverrideNativeName="Water Depth and Velocity RT"))
	UTextureRenderTarget2D* WaterDepthAndVelocityRT = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="Combined Velocity And Height RT A", Category="Render Targets", OverrideNativeName="Combined Velocity and Height RT A"))
	UTextureRenderTarget2D* CombinedVelocityAndHeightRTA = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="Combined Velocity And Height RT B", Category="Render Targets", OverrideNativeName="Combined Velocity and Height RT B"))
	UTextureRenderTarget2D* CombinedVelocityAndHeightRTB = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="LS RT Ref", Category="Render Targets", OverrideNativeName="LS RT Ref"))
	UTextureRenderTarget2D* LandscapeRTRef = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="Weightmap RT A", Category="Render Targets", OverrideNativeName="Weightmap RT A"))
	UTextureRenderTarget2D* WeightmapRTA = nullptr;
	
	UPROPERTY(VisibleAnywhere, Transient, meta=(DisplayName="Weightmap RT B", Category="Render Targets", OverrideNativeName="Weightmap RT B"))
	UTextureRenderTarget2D* WeightmapRTB = nullptr;

	UPROPERTY(VisibleAnywhere, meta = (DisplayName = "Wave Params RT", Category = "Default", OverrideNativeName = "WaveParamsRT"))
	UTextureRenderTarget2D* WaveParamsRT = nullptr;
	// RTs End

	// MIDs
	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta=(Category="Debug MIDs", OverrideNativeName="Brush Angle Falloff MID"))
	UMaterialInstanceDynamic* BrushAngleFalloffMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta=(Category="Debug MIDs", OverrideNativeName="Brush Width Falloff MID"))
	UMaterialInstanceDynamic* BrushWidthFalloffMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta=(Category="Debug MIDs", OverrideNativeName="Distance Field Cache MID"))
	UMaterialInstanceDynamic* DistanceFieldCacheMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs", OverrideNativeName = "SplineMIDs"))
	TArray<UMaterialInstanceDynamic*> RiverSplineMIDs;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs", OverrideNativeName = "Debug Distance Field MID"))
	UMaterialInstanceDynamic* DebugDistanceFieldMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs", OverrideNativeName = "WeightmapMID"))
	UMaterialInstanceDynamic* WeightmapMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs"))
	UMaterialInstanceDynamic* DrawCanvasMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs", OverrideNativeName = "CombineAlphasMID"))
	UMaterialInstanceDynamic* CombineAlphasMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs", OverrideNativeName = "Island Falloff MID"))
	UMaterialInstanceDynamic* IslandFalloffMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs", OverrideNativeName = "FinalizeVelocityHeightMID"))
	UMaterialInstanceDynamic* FinalizeVelocityHeightMID = nullptr;

	// TODO [jonathan.bard] : remove unused
	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs", OverrideNativeName = "DownsampleMID"))
	UMaterialInstanceDynamic* DownsampleMID = nullptr;
	// MIDs End

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Transient, meta=(DisplayName="Brush Curve RTCache", Category="Debug", OverrideNativeName="BrushCurveRTCache"))
	TMap<UCurveFloat*,FWaterBodyBrushCache> BrushCurveRTCache;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(DisplayName="World Size", Category="Debug", OverrideNativeName="World Size"))
	FVector WorldSize;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(DisplayName="Landscape RT Res", Category="Debug", ExposeOnSpawn="true", OverrideNativeName="Landscape RT Res"))
	FIntPoint LandscapeRTRes;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(DisplayName="Landscape Quads", Category="Debug", ExposeOnSpawn="true", OverrideNativeName="Landscape Quads"))
	FIntPoint LandscapeQuads;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(DisplayName="LS Transform", Category="Debug", OverrideNativeName="LS Transform"))
	FTransform LandscapeTransform;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Show Gradient", Category="Debug", OverrideNativeName="Show Gradient"))
	bool ShowGradient = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Distance Divisor", Category="Debug", OverrideNativeName="Distance Divisor"))
	float DistanceDivisor = 0.1f;
			
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Show Distance", Category="Debug", OverrideNativeName="Show Distance"))
	bool ShowDistance = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Show Grid", Category="Debug", OverrideNativeName="Show Grid"))
	bool ShowGrid = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Canvas Segment Size", Category="Default", OverrideNativeName="Canvas Segment Size"))
	float CanvasSegmentSize = 1024.0f;	
					
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Water Clear Height", Category="Default", OverrideNativeName="Water Clear Height"))
	float WaterClearHeight = -16384.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Spline Mesh Extension", Category="Default", OverrideNativeName="SplineMeshExtension"))
	float SplineMeshExtension = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Use Dynamic Preview RT", Category="Texture Output", OverrideNativeName="Use Dynamic Preview RT"))
	bool UseDynamicPreviewRT = false;
		
	// TODO [jonathan.bard] : remove duplicate from GerstnerWaveController? : 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Max Waves Per Water Body", Category="Default", OverrideNativeName="Max Waves Per WaterBody"))
	int32 MaxWavesPerWaterBody = 8;
		
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Disable Brush Texture Effects", Category="Debug", OverrideNativeName="Disable Brush Texture Effects"))
	bool DisableBrushTextureEffects = false;
			
	// TODO [jonathan.bard] : Rename to RenderRiverSplineDepthMaterial : 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Required"))
	UMaterialInterface* RenderSplineDepthsMaterial = nullptr;

	// TODO [jonathan.bard] : rename to DebugDistanceFieldMaterial : 
	// TODO [jonathan.bard] : put in UWaterEditorSettings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Debug DF Material", Category = "Default", OverrideNativeName = "Debug DF Material"))
	UMaterialInstance* DebugDF = nullptr;

	AWaterBrushManager(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph) override;
	virtual void BlueprintOnRenderTargetTexturesUpdated_Native(UTexture2D* VelocityTexture) override;
	virtual void BlueprintWaterBodyChanged_Native(AActor* Actor) override;
	virtual void Initialize_Native(FTransform const& InLandscapeTransform, FIntPoint const& InLandscapeSize, FIntPoint const& InLandscapeRenderTargetSize) override;
	virtual UTextureRenderTarget2D*  Render_Native(bool InIsHeightmap, UTextureRenderTarget2D* InCombinedResult, FName const& InWeightmapLayerName) override;
		
	virtual void BlueprintGetRenderTargets_Native(UTextureRenderTarget2D* InHeightRenderTarget, /*out*/ UTextureRenderTarget2D*& OutVelocityRenderTarget) override;
		
	// Debug Buttons
	UFUNCTION(BlueprintCallable, meta = (CallInEditor = "true", Category, OverrideNativeName = "Generate Wave Parameter Texture"))
	virtual void GenerateWaveParameterTexture();

	UFUNCTION(BlueprintCallable, meta = (CallInEditor = "true", Category, OverrideNativeName = "Force Update"))
	virtual void ForceUpdate();

	UFUNCTION(BlueprintCallable, meta = (CallInEditor = "true", Category, OverrideNativeName = "Single Blur Step"))
	virtual void SingleBlurStep();

	UFUNCTION(BlueprintCallable, meta = (CallInEditor = "true", Category, OverrideNativeName = "Find Edges"))
	virtual void FindEdges();

	UFUNCTION(BlueprintCallable, meta = (CallInEditor = "true", Category, OverrideNativeName = "Single Jump Step"))
	virtual void SingleJumpStep();

	// End Debug Buttons
	
	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (Category, OverrideNativeName = "Get Water Cache Key"))
	virtual void GetWaterCacheKey(AActor* WaterBrush, /*out*/ UWaterBodyBrushCacheContainer*& ContainerObject, /*out*/ FWaterBodyBrushCache& Value);

	/** 
		Sorts the water bodies in the order they should be rendered when rendering the water brush
		@param InOutWaterBodies : list of water bodies that needs sorting
	*/
	UFUNCTION(BlueprintNativeEvent)
	void SortWaterBodiesForBrushRender(TArray<AWaterBody*>& InOutWaterBodies) const;

private:
	/** Internal struct for passing information around when rendering a water brush actor. */
	struct FBrushActorRenderContext
	{
		FBrushActorRenderContext(const TWeakInterfacePtr<IWaterBrushActorInterface>& InWaterBrushActor)
			: WaterBrushActor(InWaterBrushActor)
		{}

		template <typename T>
		T* TryGetActorAs() const { return Cast<T>(WaterBrushActor.GetObject()); }

		template <typename T>
		T* GetActorAs() const { return CastChecked<T>(WaterBrushActor.GetObject()); }

		AActor* GetActor() const { return GetActorAs<AActor>(); }

		TWeakInterfacePtr<IWaterBrushActorInterface> WaterBrushActor;
		UWaterBodyBrushCacheContainer* CacheContainer = nullptr;
		UMaterialInstanceDynamic* MID = nullptr;
	};

	/** Internal struct for passing information around when rendering the whole brush. */
	struct FBrushRenderContext
	{
		bool bHeightmapRender = false; 
		FName WeightmapLayerName;
		int32 RTIndex = 0;
		int32 VelocityRTIndex = 0;
	};

private:
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool AllocateRTs();
	virtual void SetMPCParams();
	virtual void UpdateTransform(const FTransform& Transform);
	virtual bool SetupRiverSplineRenderMIDs(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void CaptureMeshDepth(const TArrayView<UStaticMeshComponent*>& MeshComponents);
	virtual void CacheBrushDistanceField(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void DrawCanvasShape(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void DrawBrushMaterial(const FBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void CaptureRiverDepthAndVelocity(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void UpdateCurves();
	virtual bool BrushRenderSetup();
	virtual void SetBrushMIDParams(const FBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext);
	virtual void UpdateCurveCacheKeys();
	virtual void UpdateBrushCacheKeys();
	virtual void RenderBrushActorContext(FBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext);
	virtual bool CreateMIDs();
	virtual void DistanceFieldCaching(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void CurvesSmoothingAndTerracing(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void FalloffAndBlendMode(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void DisplacementSettings(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void ApplyWeightmapSettings(const FBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext, const FWaterBodyWeightmapSettings& WMSettings);
	virtual void ApplyToCombinedAlphas(FBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext);

	UCurveFloat* GetElevationCurveAsset(const FWaterCurveSettings& CurveSettings);
	void ClearCurveCache();
	void OnCurveUpdated(UCurveBase* Curve, EPropertyChangeType::Type ChangeType);

	UTextureRenderTarget2D* VelocityPingPongRead(const FBrushRenderContext& BrushRenderContext) const;
	UTextureRenderTarget2D* VelocityPingPongWrite(const FBrushRenderContext& BrushRenderContext) const;

	UTextureRenderTarget2D* HeightPingPongRead(const FBrushRenderContext& BrushRenderContext) const;
	UTextureRenderTarget2D* HeightPingPongWrite(const FBrushRenderContext& BrushRenderContext) const;

	UTextureRenderTarget2D* WeightPingPongRead(const FBrushRenderContext& BrushRenderContext) const;
	UTextureRenderTarget2D* WeightPingPongWrite(const FBrushRenderContext& BrushRenderContext) const;


private:
	bool bKillCache = false;
	int32 LastRenderedVelocityRTIndex = 0;
};
