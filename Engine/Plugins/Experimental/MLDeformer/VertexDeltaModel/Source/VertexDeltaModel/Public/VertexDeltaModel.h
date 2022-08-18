// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModel.h"
#include "MLDeformerVizSettings.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "UObject/Object.h"
#include "VertexDeltaModel.generated.h"

class USkeletalMesh;
class UGeometryCache;
class UAnimSequence;
class UNeuralNetwork;
class UMLDeformerAsset;
class USkeleton;
class IPropertyHandle;


UCLASS()
class VERTEXDELTAMODEL_API UVertexDeltaModel 
	: public UMLDeformerModel
{
	GENERATED_BODY()

public:
	UVertexDeltaModel(const FObjectInitializer& ObjectInitializer);

	// UMLDeformerModel overrides.
	virtual FString GetDisplayName() const override { return "Vertex Delta Model"; }
#if WITH_EDITORONLY_DATA
	virtual bool HasTrainingGroundTruth() const override { return (GeometryCache.Get() != nullptr); }
	virtual void SampleGroundTruthPositions(float SampleTime, TArray<FVector3f>& OutPositions) override;
#endif
#if WITH_EDITOR
	virtual void UpdateNumTargetMeshVertices() override;
	virtual void SetAssetEditorOnlyFlags() override;
#endif
	// ~END UMLDeformerModel overrides.

#if WITH_EDITORONLY_DATA
	const UGeometryCache* GetGeometryCache() const { return GeometryCache; }
	UGeometryCache* GetGeometryCache() { return GeometryCache; }
	int32 GetNumHiddenLayers() const { return NumHiddenLayers; }
	int32 GetNumNeuronsPerLayer() const { return NumNeuronsPerLayer; }
	int32 GetNumIterations() const { return NumIterations; }
	int32 GetBatchSize() const { return BatchSize; }
	float GetLearningRate() const { return LearningRate; }
	TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping>& GetGeomCacheMeshMappings() { return MeshMappings; }
	const TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping>& GetGeomCacheMeshMappings() const { return MeshMappings; }
#endif

public:

#if WITH_EDITORONLY_DATA
	TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping> MeshMappings;

	/** The geometry cache that represents the complex mesh deformations. */
	UPROPERTY(EditAnywhere, Category = "Target Mesh")
	TObjectPtr<UGeometryCache> GeometryCache = nullptr;

	/** The number of hidden layers that the neural network model will have.\nHigher numbers will slow down performance but can deal with more complex deformations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1", ClampMax = "10"))
	int32 NumHiddenLayers = 3;

	/** The number of units/neurons per hidden layer. Higher numbers will slow down performance but allow for more complex mesh deformations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 NumNeuronsPerLayer = 256;

	/** The number of iterations to train the model for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 NumIterations = 10000;

	/** The number of frames per batch when training the model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 BatchSize = 128;

	/** The learning rate used during the model training. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "0.000001", ClampMax = "1.0"))
	float LearningRate = 0.001f;
#endif // WITH_EDITORONLY_DATA
};
