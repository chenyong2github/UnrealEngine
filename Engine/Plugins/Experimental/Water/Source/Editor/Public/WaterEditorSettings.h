// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/TextureDefines.h"
#include "Engine/DeveloperSettings.h"
#include "WaterCurveSettings.h"
#include "WaterBodyHeightmapSettings.h"
#include "WaterBodyWeightmapSettings.h"
#include "WaterSplineMetadata.h"
#include "WaterEditorSettings.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;
class UStaticMesh;
class UWaterWavesBase;
class AWaterLandscapeBrush;

USTRUCT()
struct WATEREDITOR_API FWaterBrushActorDefaults
{
	GENERATED_BODY()

	FWaterBrushActorDefaults();

	UPROPERTY(EditAnywhere, config, Category = Terrain)
	FWaterCurveSettings CurveSettings;

	UPROPERTY(EditAnywhere, config, Category = Terrain)
	FWaterBodyHeightmapSettings HeightmapSettings;

	UPROPERTY(EditAnywhere, config, Category = Terrain)
	TMap<FName, FWaterBodyWeightmapSettings> LayerWeightmapSettings;
}; 


USTRUCT()
struct WATEREDITOR_API FWaterBodyDefaults
{
	GENERATED_BODY()

	FWaterBodyDefaults();

	UPROPERTY(EditAnywhere, config, Category = "Water Spline")
	FWaterSplineCurveDefaults SplineDefaults;

public: 
	UMaterialInterface* GetWaterMaterial() const;
	FSoftObjectPath GetWaterMaterialPath() const { return WaterMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetUnderwaterPostProcessMaterial() const;
	FSoftObjectPath GetUnderwaterPostProcessMaterialPath() const { return UnderwaterPostProcessMaterial.ToSoftObjectPath(); }

protected:
	UPROPERTY(EditAnywhere, config, Category = Rendering)
	TSoftObjectPtr<UMaterialInterface> WaterMaterial;

	UPROPERTY(EditAnywhere, config, Category = Rendering)
	TSoftObjectPtr<UMaterialInterface> UnderwaterPostProcessMaterial;
};

USTRUCT()
struct WATEREDITOR_API FWaterBodyRiverDefaults : public FWaterBodyDefaults
{
	GENERATED_BODY()

	FWaterBodyRiverDefaults();

	UPROPERTY(EditAnywhere, config, Category = Terrain)
	FWaterBrushActorDefaults BrushDefaults;

	UMaterialInterface* GetRiverToOceanTransitionMaterial() const;
	FSoftObjectPath GetRiverToOceanTransitionTransitionMaterialPath() const { return RiverToOceanTransitionMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetRiverToLakeTransitionMaterial() const;
	FSoftObjectPath GetRiverToLakeTransitionTransitionMaterialPath() const { return RiverToLakeTransitionMaterial.ToSoftObjectPath(); }

protected:
	UPROPERTY(EditAnywhere, config, Category = Rendering)
	TSoftObjectPtr<UMaterialInterface> RiverToOceanTransitionMaterial;

	UPROPERTY(EditAnywhere, config, Category = Rendering)
	TSoftObjectPtr<UMaterialInterface> RiverToLakeTransitionMaterial;
};


USTRUCT()
struct WATEREDITOR_API FWaterBodyLakeDefaults : public FWaterBodyDefaults
{
	GENERATED_BODY()
	
	FWaterBodyLakeDefaults();

	UPROPERTY(EditAnywhere, config, Category = Terrain)
	FWaterBrushActorDefaults BrushDefaults;

	UPROPERTY(EditAnywhere, Instanced, Category = Wave)
	UWaterWavesBase* WaterWaves = nullptr;
};


USTRUCT()
struct WATEREDITOR_API FWaterBodyOceanDefaults : public FWaterBodyDefaults
{
	GENERATED_BODY()

	FWaterBodyOceanDefaults();

	UPROPERTY(EditAnywhere, config, Category = Terrain)
	FWaterBrushActorDefaults BrushDefaults;

	UPROPERTY(EditAnywhere, Instanced, Category = Wave)
	UWaterWavesBase* WaterWaves = nullptr;
};


USTRUCT()
struct WATEREDITOR_API FWaterBodyCustomDefaults : public FWaterBodyDefaults
{
	GENERATED_BODY()

	FWaterBodyCustomDefaults();

	UStaticMesh* GetWaterMesh() const;
	FSoftObjectPath GetWaterMeshPath() const { return WaterMesh.ToSoftObjectPath(); }

private:
	UPROPERTY(EditAnywhere, config, Category = Rendering)
	TSoftObjectPtr<UStaticMesh> WaterMesh;
};


USTRUCT()
struct WATEREDITOR_API FWaterBodyIslandDefaults 
{
	GENERATED_BODY()

	FWaterBodyIslandDefaults();

	UPROPERTY(EditAnywhere, config, Category = Terrain)
	FWaterBrushActorDefaults BrushDefaults;
};


/**
 * Implements the editor settings for the Water plugin.
 */
UCLASS(config = Engine, defaultconfig, meta=(DisplayName="Water Editor"))
class WATEREDITOR_API UWaterEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWaterEditorSettings();

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	TSubclassOf<AWaterLandscapeBrush> GetWaterManagerClass() const;
	FSoftClassPath GetWaterManagerClassPath() const { return WaterManagerClassPath; }

	UMaterialInterface* GetDefaultBrushAngleFalloffMaterial() const;
	FSoftObjectPath GetDefaultBrushAngleFalloffMaterialPath() const { return DefaultBrushAngleFalloffMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultBrushIslandFalloffMaterial() const;
	FSoftObjectPath GetDefaultBrushIslandFalloffMaterialPath() const { return DefaultBrushIslandFalloffMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultBrushWidthFalloffMaterial() const;
	FSoftObjectPath GetDefaultBrushWidthFalloffMaterialPath() const { return DefaultBrushWidthFalloffMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultBrushWeightmapMaterial() const;
	FSoftObjectPath GetDefaultBrushWeightmapMaterialPath() const { return DefaultBrushWeightmapMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultCacheDistanceFieldCacheMaterial() const;
	FSoftObjectPath GetDefaultCacheDistanceFieldCacheMaterialPath() const { return DefaultCacheDistanceFieldCacheMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultCompositeWaterBodyTextureMaterial() const;
	FSoftObjectPath GetDefaultCompositeWaterBodyTextureMaterialPath() const { return DefaultCompositeWaterBodyTextureMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultFinalizeVelocityHeightMaterial() const;
	FSoftObjectPath GetDefaultFinalizeVelocityHeightMaterialPath() const { return DefaultFinalizeVelocityHeightMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultJumpFloodStepMaterial() const;
	FSoftObjectPath GetDefaultJumpFloodStepMaterialPath() const { return DefaultJumpFloodStepMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultBlurEdgesMaterial() const;
	FSoftObjectPath GetDefaultBlurEdgesMaterialPath() const { return DefaultBlurEdgesMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultFindEdgesMaterial() const;
	FSoftObjectPath GetDefaultFindEdgesMaterialPath() const { return DefaultFindEdgesMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultDrawCanvasMaterial() const;
	FSoftObjectPath GetDefaultDrawCanvasMaterialPath() const { return DefaultDrawCanvasMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultRenderRiverSplineDepthsMaterial() const;
	FSoftObjectPath GetDefaultRenderRiverSplineDepthsMaterialPath() const { return DefaultRenderRiverSplineDepthsMaterial.ToSoftObjectPath(); }

public:
	/** The texture group to use for generated textures such as the combined veloctiy and height texture */
	UPROPERTY(EditAnywhere, config, Category = Rendering)
	TEnumAsByte<TextureGroup> TextureGroupForGeneratedTextures;

	/** Maximum size of the water velocity/height texture for a WaterMeshActor */
	UPROPERTY(EditAnywhere, config, Category = Rendering, meta=(ClampMin=1, ClampMax=2048))
	int32 MaxWaterVelocityAndHeightTextureSize;

	/** Scale factor for visualizing water velocity */
	UPROPERTY(EditAnywhere, Category = "Rendering", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float VisualizeWaterVelocityScale = 20.0f;

	/** Material Parameter Collection for everything landscape-related */
	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialParameterCollection> LandscapeMaterialParameterCollection;

	/** Default values for base WaterBodyRiver actor */
	UPROPERTY(EditAnywhere, config, Category = ActorDefaults)
	FWaterBodyRiverDefaults WaterBodyRiverDefaults;

	/** Default values for base WaterBodyLake actor */
	UPROPERTY(EditAnywhere, config, Category = ActorDefaults)
	FWaterBodyLakeDefaults WaterBodyLakeDefaults;

	/** Default values for base WaterBodyOcean actor */
	UPROPERTY(EditAnywhere, config, Category = ActorDefaults)
	FWaterBodyOceanDefaults WaterBodyOceanDefaults;

	/** Default values for base WaterBodyCustom actor */
	UPROPERTY(EditAnywhere, config, Category = ActorDefaults)
	FWaterBodyCustomDefaults WaterBodyCustomDefaults;

	/** Default values for base WaterBodyIsland actor */
	UPROPERTY(EditAnywhere, config, Category = ActorDefaults)
	FWaterBodyIslandDefaults WaterBodyIslandDefaults;

private:
	/** Class of the water brush to be used in landscape */
	UPROPERTY(EditAnywhere, config, Category = Brush, meta = (MetaClass = "WaterLandscapeBrush"))
	FSoftClassPath WaterManagerClassPath;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultBrushAngleFalloffMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultBrushIslandFalloffMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultBrushWidthFalloffMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultBrushWeightmapMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultCacheDistanceFieldCacheMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultCompositeWaterBodyTextureMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultFinalizeVelocityHeightMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultJumpFloodStepMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultBlurEdgesMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultFindEdgesMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultDrawCanvasMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush)
	TSoftObjectPtr<UMaterialInterface> DefaultRenderRiverSplineDepthsMaterial;
};
