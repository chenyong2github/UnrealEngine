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
	DisplacementMap UMETA(DisplayName = "Displacement Map"),

	/** Move vertices in spatial sine wave pattern */
	SineWave UMETA(DisplayName = "Sine Wave"),
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
	UDisplaceMeshTool();

	void Setup() final;
	void Shutdown(EToolShutdownType ShutdownType) final;
	void Tick(float DeltaTime) final;

	bool HasCancel() const final { return true; }
	bool HasAccept() const final { return true; }
	bool CanAccept() const final { return true; }
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) final;
#endif

	/** primary brush mode */
	UPROPERTY(EditAnywhere, Category = Options)
	EDisplaceMeshToolDisplaceType DisplacementType;

	/** Displacement intensity */
	UPROPERTY(EditAnywhere, Category = Options,
		meta = (UIMin = "-100.0", UIMax = "100.0", ClampMin = "-10000.0", ClampMax = "100000.0"))
	float DisplaceIntensity;

	/** Seed for randomization */
	UPROPERTY(EditAnywhere, Category = Options,
		meta = (EditCondition = "DisplacementType == EDisplaceMeshToolDisplaceType::RandomNoise || DisplacementType == EDisplaceMeshToolDisplaceType::PerlinNoise"))
	int RandomSeed;

	/** Subdivision iterations for mesh */
	UPROPERTY(EditAnywhere, Category = Options,
		meta = (UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "100"))
	int Subdivisions;

	/** Displacement map */
	UPROPERTY(EditAnywhere, Category = Options,
		meta = (EditCondition = "DisplacementType == EDisplaceMeshToolDisplaceType::DisplacementMap"))
	UTexture2D* DisplacementMap;

	/** Displacement frequency */
	UPROPERTY(EditAnywhere, Category = Options,
		meta = (EditCondition = "DisplacementType == EDisplaceMeshToolDisplaceType::SineWave || DisplacementType == EDisplaceMeshToolDisplaceType::PerlinNoise", 
				UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "10.0"))
	float DisplaceFrequency;

	/** Displacement phase shift */
	UPROPERTY(EditAnywhere, Category = Options,
		meta = (EditCondition = "DisplacementType == EDisplaceMeshToolDisplaceType::SineWave", 
				UIMin = "0.0", UIMax = "6.28318531", ClampMin = "0.0", ClampMax = "6.28318531"))
	float DisplacePhaseShift;

private:
	void StartComputation();
	void AdvanceComputation();
	void ValidateSubdivisions();

	FAsyncTaskExecuterWithAbort<TModelingOpTask<FDynamicMeshOperator>>* SubdivideTask = nullptr;
	bool bNeedsSubdivided = true;
	FAsyncTaskExecuterWithAbort<TModelingOpTask<FDynamicMeshOperator>>* DisplaceTask = nullptr;
	bool bNeedsDisplaced = true;

	FDynamicMesh3 OriginalMesh;
	TSharedPtr<FDynamicMesh3> SubdividedMesh = nullptr;
	USimpleDynamicMeshComponent* DynamicMeshComponent = nullptr;

	TUniquePtr<IDynamicMeshOperatorFactory> Subdivider = nullptr;
	TUniquePtr<IDynamicMeshOperatorFactory> Displacer = nullptr;
};
