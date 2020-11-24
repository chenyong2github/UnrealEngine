// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/Classes/Engine/Texture2D.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "MeshOpPreviewHelpers.h"
#include "Spatial/SampledScalarField2.h"
#include "WeightMapUtil.h"
#include "DisplaceMeshTool.generated.h"

struct FMeshDescription;
class USimpleDynamicMeshComponent;

UENUM()
enum class EDisplaceMeshToolDisplaceType : uint8
{
	/** Displace with N iterations */
	Constant UMETA(DisplayName = "Constant"),

	/** Displace with N iterations */
	RandomNoise UMETA(DisplayName = "Random Noise"),

	/** Offset in the normal direction weighted by Perlin noise. 
	    We use the following formula to compute the weighting for each vertex:
			w = PerlinNoise3D(f * (X + r))
		Where f is a frequency parameter, X is the vertex position, and r is a randomly-generated offset (using the Seed property).
		Note the range of 3D Perlin noise is [-sqrt(3/4), sqrt(3/4)].
	*/
	PerlinNoise UMETA(DisplayName = "Perlin Noise"),

	/** Displace with N iterations */
	DisplacementMap UMETA(DisplayName = "Texture2D Map"),

	/** Move vertices in spatial sine wave pattern */
	SineWave UMETA(DisplayName = "Sine Wave"),
};

/** The basic set of properties shared by (more or less) all DisplacementTypes. */
UCLASS()
class MESHMODELINGTOOLS_API UDisplaceMeshCommonProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Displacement type */
	UPROPERTY(EditAnywhere, Category = Options)
	EDisplaceMeshToolDisplaceType DisplacementType = EDisplaceMeshToolDisplaceType::PerlinNoise;

	/** Displacement intensity */
	UPROPERTY(EditAnywhere, Category = Options,
		meta = (UIMin = "-100.0", UIMax = "100.0", ClampMin = "-10000.0", ClampMax = "100000.0"))
	float DisplaceIntensity = 10.0f;

	/** Seed for randomization */
	UPROPERTY(EditAnywhere, Category = Options,
		meta = (EditCondition = "DisplacementType == EDisplaceMeshToolDisplaceType::RandomNoise || DisplacementType == EDisplaceMeshToolDisplaceType::PerlinNoise"))
	int RandomSeed = 31337;

	/** Subdivision iterations for mesh */
	UPROPERTY(EditAnywhere, Category = Options,
		meta = (UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "100"))
	int Subdivisions = 4;

	/** Select vertex weight map. If configured, the weight map value will be sampled to modulate displacement intensity. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (GetOptions = GetWeightMapsFunc))
	FName WeightMap;

	UFUNCTION()
	TArray<FString> GetWeightMapsFunc();

	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> WeightMapsList;

	UPROPERTY(EditAnywhere, Category = Options)
	bool bInvertWeightMap = false;

	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay, meta = (TransientToolProperty) )
	bool bDisableSizeWarning = false;
};



/** PropertySet for properties affecting the Image Map displacement type. */
UCLASS()
class MESHMODELINGTOOLS_API UDisplaceMeshTextureMapProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
			
public:
	/** Displacement map */
	UPROPERTY(EditAnywhere, Category = Options)
	UTexture2D* DisplacementMap = nullptr;
};



/** Properties for a directional filter. Allows for displacement to be applied only to vertices whose normals point in a given direction */
UCLASS()
class MESHMODELINGTOOLS_API UDisplaceMeshDirectionalFilterProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Whether the directional filter is active. */
	UPROPERTY(EditAnywhere, Category = DirectionalFilterOptions)
	bool bEnableFilter = false;

	/** Unit vector representing the direction to filter along. */
	UPROPERTY(EditAnywhere, Category = DirectionalFilterOptions, 
		meta = (EditCondition = "bEnableFilter == true"))
	FVector FilterDirection = {0.0f, 0.0f, 1.0f};

	/** Scalar value determining how close to the filter direction the vertex normals must be in order to be displaced.
		0: Only normals pointing exactly in the filter direction are displaced.
		0.5: Normals forming angle up to 90 from the filter direction are displaced.
		1.0: All vertices are displaced.
	*/
	UPROPERTY(EditAnywhere, Category = DirectionalFilterOptions,
		meta = (EditCondition = "bEnableFilter == true", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float FilterWidth = 0.25f;

};

/** Per-layer properties for Perlin noise. Each layer has independent Frequency and Intensity. */
USTRUCT()
struct FPerlinLayerProperties
{
	GENERATED_BODY()

	FPerlinLayerProperties() = default;

	FPerlinLayerProperties(float FrequencyIn, float IntensityIn) : 
		Frequency(FrequencyIn), 
		Intensity(IntensityIn) 
    {}

	/** Frequency of Perlin noise layer */
	UPROPERTY(EditAnywhere, Category = PerlinNoiseOptions,
			meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "10.0"))
	float Frequency = 0.1f;

	/** Intensity/amplitude of Perlin noise layer */
	UPROPERTY(EditAnywhere, Category = PerlinNoiseOptions,
		meta = (UIMin = "-10.0", UIMax = "10.0", ClampMin = "-100.0", ClampMax = "100.0"))
	float Intensity = 1.0f;
};

/** PropertySet for properties affecting the Perlin Noise displacement type. */
UCLASS()
class MESHMODELINGTOOLS_API UDisplaceMeshPerlinNoiseProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = PerlinNoiseOptions)
	TArray<FPerlinLayerProperties> PerlinLayerProperties = 
		{ /*{Frequency, Intensity}*/ {0.05f, 1.0f}, {0.25f, 0.5f}, {0.5f, 0.2f}, {1.0f, 0.1f} };
};


/** PropertySet for Sine wave displacement */
UCLASS()
class MESHMODELINGTOOLS_API UDisplaceMeshSineWaveProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Sine wave displacement frequency */
	UPROPERTY(EditAnywhere, Category = SineWaveOptions,
		meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "10.0"))
	float SineWaveFrequency = 0.1f;

	/** Sine wave phase shift */
	UPROPERTY(EditAnywhere, Category = SineWaveOptions,
		meta = (UIMin = "0.0", UIMax = "6.28318531", ClampMin = "0.0", ClampMax = "6.28318531"))
	float SineWavePhaseShift = 0.0f;

	/** Unit vector representing the direction of wave displacement. */
	UPROPERTY(EditAnywhere, Category = SineWaveOptions)
	FVector SineWaveDirection = { 0.0f, 0.0f, 1.0f };
};

/**
 * Builder for Simple Mesh Displacement Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UDisplaceMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:
	bool CanBuildTool(const FToolBuilderState& SceneState) const final;
	UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const final;
};

/**
 * Simple Mesh Displacement Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UDisplaceMeshTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	void Setup() final;
	void Shutdown(EToolShutdownType ShutdownType) final;
	void OnTick(float DeltaTime) final;

	bool HasCancel() const final { return true; }
	bool HasAccept() const final { return true; }

#if WITH_EDITOR
	void OnPropertyModified(UObject* PropertySet, FProperty* Property) final;
#endif

	/** Shared properties for all displacement modes. */
	UPROPERTY()
	UDisplaceMeshCommonProperties* CommonProperties;

	/** Properties defining the directional filter. */
	UPROPERTY()
	UDisplaceMeshDirectionalFilterProperties* DirectionalFilterProperties;

	/** Properties defining the texture map */
	UPROPERTY()
	UDisplaceMeshTextureMapProperties* TextureMapProperties;

	/** Multi-layer Perlin noise frequencies and intensities */
	UPROPERTY()
	UDisplaceMeshPerlinNoiseProperties* NoiseProperties;

	/** Sine wave parameters and direction of displacement */
	UPROPERTY()
	UDisplaceMeshSineWaveProperties* SineWaveProperties;

private:
	void StartComputation();
	void AdvanceComputation();
	void ValidateSubdivisions();

	FAsyncTaskExecuterWithAbort<TModelingOpTask<FDynamicMeshOperator>>* SubdivideTask = nullptr;
	bool bNeedsSubdivided = true;
	FAsyncTaskExecuterWithAbort<TModelingOpTask<FDynamicMeshOperator>>* DisplaceTask = nullptr;
	bool bNeedsDisplaced = true;

	FDynamicMesh3 OriginalMesh;
	FDynamicMeshAABBTree3 OriginalMeshSpatial;
	TSharedPtr<FIndexedWeightMap> ActiveWeightMap;
	void UpdateActiveWeightMap();
	float WeightMapQuery(const FVector3d& Position, const FIndexedWeightMap& WeightMap) const;

	TSharedPtr<FDynamicMesh3> SubdividedMesh = nullptr;
	USimpleDynamicMeshComponent* DynamicMeshComponent = nullptr;

	TUniquePtr<IDynamicMeshOperatorFactory> Subdivider = nullptr;
	TUniquePtr<IDynamicMeshOperatorFactory> Displacer = nullptr;
};
