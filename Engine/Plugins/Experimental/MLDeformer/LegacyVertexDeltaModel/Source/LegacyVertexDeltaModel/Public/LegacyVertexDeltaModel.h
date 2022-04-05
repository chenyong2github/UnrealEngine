// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModel.h"
#include "MLDeformerVizSettings.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "UObject/Object.h"
#include "LegacyVertexDeltaModel.generated.h"

class USkeletalMesh;
class UGeometryCache;
class UAnimSequence;
class UNeuralNetwork;
class UMLDeformerAsset;
class USkeleton;
class IPropertyHandle;

/** The activation function to use during the ML Deformer training process. */
UENUM()
enum class ELegacyVertexDeltaModelActivationFunction : uint8
{
	Relu,
	LRelu,
	Tanh
};

/** The loss function to use during the ML Deformer training process. */
UENUM()
enum class ELegacyVertexDeltaModelLossFunction : uint8
{
	L1,
	MSE,
	Shrinkage
};

UCLASS()
class LEGACYVERTEXDELTAMODEL_API ULegacyVertexDeltaModel
	: public UMLDeformerModel
{
	GENERATED_BODY()

public:
	ULegacyVertexDeltaModel(const FObjectInitializer& ObjectInitializer);

	// UMLDeformerModel overrides.
	virtual FString GetDisplayName() const override { return "Legacy Vertex Delta Model"; }
#if WITH_EDITORONLY_DATA
	virtual bool HasTrainingGroundTruth() const override { return (GeometryCache.Get() != nullptr); }
	virtual void SampleGroundTruthPositions(float SampleTime, TArray<FVector3f>& OutPositions) override;
#endif
#if WITH_EDITOR
	virtual void UpdateNumTargetMeshVertices();
#endif
	// ~END UMLDeformerModel overrides.

	const FVector& GetVertexDeltaMean() const { return VertexDeltaMean; }
	const FVector& GetVertexDeltaScale() const { return VertexDeltaScale; }

#if WITH_EDITORONLY_DATA
	const UGeometryCache* GetGeometryCache() const { return GeometryCache; }
	UGeometryCache* GetGeometryCache() { return GeometryCache; }
	ELegacyVertexDeltaModelLossFunction GetLossFunction() const { return LossFunction; }
	ELegacyVertexDeltaModelActivationFunction GetActivationFunction() const { return ActivationFunction; }
	int32 GetNumHiddenLayers() const { return NumHiddenLayers; }
	int32 GetNumNeuronsPerLayer() const { return NumNeuronsPerLayer; }
	int32 GetNumEpochs() const { return Epochs; }
	int32 GetBatchSize() const { return BatchSize; }
	float GetLearningRate() const { return LearningRate; }
	float GetShrinkageSpeed() const { return ShrinkageSpeed; }
	float GetShrinkageThrehsold() const { return ShrinkageThreshold; }
	int32 GetMaxCacheSizeGB() const { return MaxCacheSizeGB; }
	TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping>& GetGeomCacheMeshMappings() { return MeshMappings; }
	const TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping>& GetGeomCacheMeshMappings() const { return MeshMappings; }
#endif

public:
	/** The vertex delta mean. This is passed to the deformer shader to reconstruct the correct output deltas. */
	UPROPERTY()
	FVector VertexDeltaMean = FVector::ZeroVector;

	/** The vertex delta scale. This is passed to the deformer shader to rescale the output deltas. */
	UPROPERTY()
	FVector VertexDeltaScale = FVector::OneVector;

#if WITH_EDITORONLY_DATA
	TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping> MeshMappings;

	/** The geometry cache that represents the complex mesh deformations. */
	UPROPERTY(EditAnywhere, Category = "Target Mesh")
	TObjectPtr<UGeometryCache> GeometryCache = nullptr;

	/** The number of hidden layers that the neural network model will have.\nHigher numbers will slow down performance but can deal with more complex deformations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1", ClampMax = "10"))
	int32 NumHiddenLayers = 2;

	/** The number of units/neurons per hidden layer. Higher numbers will slow down performance but allow for more complex mesh deformations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 NumNeuronsPerLayer = 256;

	/** The number of epochs to process without any decay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 Epochs = 20;

	/** The number of frames per batch when training the model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 BatchSize = 128;

	/** The learning rate used during the model training. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "0.000001", ClampMax = "1.0"))
	float LearningRate = 0.00175f;

	/** The activation function to use in the neural network. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings")
	ELegacyVertexDeltaModelActivationFunction ActivationFunction = ELegacyVertexDeltaModelActivationFunction::LRelu;

	/** The loss function to use during model training. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings")
	ELegacyVertexDeltaModelLossFunction LossFunction = ELegacyVertexDeltaModelLossFunction::L1;

	/** Shrinkage speed. Only if the shrinkage loss is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float ShrinkageSpeed = 10.0f;

	/** Shrinkage threshold. Only if the shrinkage loss is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "0.0"))
	float ShrinkageThreshold = 0.1f;

	/**
	 * The maximum allowed size of the training cache in memory, in gigabytes.
	 * So a value of 4 would use a maximum of four gigabyte of system memory.
	 * The larger the cache size the faster the training.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", DisplayName = "Max Cache Size", meta = (ClampMin = "0", ForceUnits = "Gigabytes"))
	int32 MaxCacheSizeGB = 4;	// 4 Gigabyte

#endif // WITH_EDITORONLY_DATA
};
