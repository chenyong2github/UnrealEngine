// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModel.h"
#include "MLDeformerVizSettings.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "UObject/Object.h"
#include "NeuralMorphModel.generated.h"

class USkeletalMesh;
class UGeometryCache;
class UAnimSequence;
class UNeuralNetwork;
class UMLDeformerAsset;
class UMLDeformerModelInstance;
class USkeleton;
class IPropertyHandle;
struct FExternalMorphSet;

UCLASS()
class NEURALMORPHMODEL_API UNeuralMorphModel 
	: public UMLDeformerModel
{
	GENERATED_BODY()

public:
	UNeuralMorphModel(const FObjectInitializer& ObjectInitializer);

	// UMLDeformerModel overrides.
	virtual FString GetDisplayName() const override { return "Neural Morph Model"; }
	virtual void Serialize(FArchive& Archive) override;
	virtual void PostMLDeformerComponentInit(UMLDeformerModelInstance* ModelInstance) override;
	virtual bool IsNeuralNetworkOnGPU() const override { return false; }	// CPU neural network.
	virtual UMLDeformerModelInstance* CreateModelInstance(UMLDeformerComponent* Component) override;
#if WITH_EDITORONLY_DATA
	virtual bool HasTrainingGroundTruth() const override { return (GeometryCache.Get() != nullptr); }
	virtual void SampleGroundTruthPositions(float SampleTime, TArray<FVector3f>& OutPositions) override;
#endif
#if WITH_EDITOR
	virtual void UpdateNumTargetMeshVertices();
#endif
	// ~END UMLDeformerModel overrides.

	// UObject overrides.
	void BeginDestroy() override;
	// ~END UObject overrides.

#if WITH_EDITORONLY_DATA
	const UGeometryCache* GetGeometryCache() const { return GeometryCache; }
	UGeometryCache* GetGeometryCache() { return GeometryCache; }
	int32 GetNumHiddenLayers() const { return NumHiddenLayers; }
	int32 GetNumNeuronsPerLayer() const { return NumNeuronsPerLayer; }
	int32 GetNumIterations() const { return NumIterations; }
	int32 GetBatchSize() const { return BatchSize; }
	float GetLearningRate() const { return LearningRate; }
	float GetRegularizationFactor() const { return RegularizationFactor;  }
	float GetMorphTargetDeltaThreshold() const { return MorphTargetDeltaThreshold; }
	TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping>& GetGeomCacheMeshMappings() { return MeshMappings; }
	const TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping>& GetGeomCacheMeshMappings() const { return MeshMappings; }
#endif

	UFUNCTION(BlueprintCallable, Category = "NeuralMorphModel")
	void SetMorphTargetDeltas(const TArray<float>& Deltas);

	const TArray<FVector3f>& GetMorphTargetDeltas() const { return MorphTargetDeltas; }
	int32 GetMorphTargetDeltaStartIndex(int32 MorphTargetIndex) const;

public:
	/** The compressed morph target data, ready for the GPU. */
	TSharedPtr<FExternalMorphSet> MorphTargetSet;

	/** The morph target set ID that is passed to FSkeletalMeshLODRenderData::AddExternalMorphBuffer(...). */
	static int32 NeuralMorphsExternalMorphSetID;

	/** 
	 * The entire set of morph target deltas, 3 per vertex, for each morph target, as one flattened buffer. 
	 * So the size of this buffer is: (NumVertsPerMorphTarget * 3 * NumMorphTargets).
	 */
	TArray<FVector3f> MorphTargetDeltas;

#if WITH_EDITORONLY_DATA
	TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping> MeshMappings;

	/** The geometry cache that represents the complex mesh deformations. */
	UPROPERTY(EditAnywhere, Category = "Target Mesh")
	TObjectPtr<UGeometryCache> GeometryCache = nullptr;

	/** The number of morph targets to generate per bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1", ClampMax = "100"))
	int32 NumMorphTargetsPerBone = 6;

	/** The number of hidden layers that the neural network model will have.\nHigher numbers will slow down performance but can deal with more complex deformations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1", ClampMax = "10"))
	int32 NumHiddenLayers = 1;

	/** The number of units/neurons per hidden layer. Higher numbers will slow down performance but allow for more complex mesh deformations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 NumNeuronsPerLayer = 6;

	/** The number of iterations to train the model for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 NumIterations = 2000;

	/** The number of frames per batch when training the model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 BatchSize = 128;

	/** The learning rate used during the model training. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "0.000001", ClampMax = "1.0"))
	float LearningRate = 0.01f;

	/** 
	 * The regularization factor. Higher values can help generate more sparse morph targets, but can also lead to visual artifacts. 
	 * A value of 0 disables the regularization, and gives the highest quality, at the cost of higher runtime memory usage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float RegularizationFactor = 1.0f;

	/** 
	 * Morph target delta values that are smaller than or equal to this threshold will be zeroed out.
	 * This essentially removes small deltas from morph targets, which will lower the memory usage at runtime, however when set too high it can also introduce visual artifacts.
	 * A value of 0 will result in the highest quality morph targets, at the cost of higher runtime memory usage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MorphTargetDeltaThreshold = 0.0025f;

	/** The morph target error tolerance. Higher values result in larger compression, but could result in visual artifacts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "0.01", ClampMax = "10000.0"))
	float MorphTargetErrorTolerance = 20.0f;
#endif // WITH_EDITORONLY_DATA
};
