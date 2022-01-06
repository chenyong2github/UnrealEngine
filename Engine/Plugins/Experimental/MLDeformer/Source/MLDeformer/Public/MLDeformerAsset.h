// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MLDeformerVizSettings.h"
#include "MLDeformerInputInfo.h"
#include "RenderCommandFence.h"
#include "UObject/Object.h"
#include "RenderResource.h"
#include "MLDeformerAsset.generated.h"

// Forward declarations.
class UNeuralNetwork;
class USkeletalMesh;
class UGeometryCache;
class UAnimSequence;
class UMLDeformerAsset;

/** The activation function to use during the ML Deformer training process. */
UENUM()
enum class EActivationFunction : uint8
{
	Relu,
	LRelu,
	Tanh
};

/** The loss function to use during the ML Deformer training process. */
UENUM()
enum class ELossFunction : uint8
{
	L1,
	MSE,
	Shrinkage
};

/** The decay function to adapt the learning rate during the ML Deformer training process. */
UENUM()
enum class EDecayFunction : uint8
{
	Linear,

	// Multiply the learning rate with the Decay Rate each step.
	// Basically making it smaller all the time.
	Multiplicative
};

/** The device where the training and testing will be running. */
UENUM()
enum class EDeviceType : uint8
{
	// Train using the CPU. This tends to be slower.
	CPU,

	// Train using the GPU. This should be the fastest.
	GPU
};

UENUM()
enum class EDeltaMode : uint8
{
	PreSkinning,	/** Apply the deltas before skinning. */
	PostSkinning	/** Apply the deltas after skinning. */
};

/** The training inputs. Specifies what data to include in training. */
UENUM()
enum class ETrainingInputs : uint8
{
	BonesAndCurves = 0, /** Include both bone rotations and curve values. */
	BonesOnly,			/** Include only bone rotations. */
	CurvesOnly			/** Include only curve values. */
};

class FVertexMapBuffer : public FVertexBufferWithSRV
{
public:
	void Init(const TArray<int32>& InVertexMap)
	{
		VertexMap = InVertexMap;
	}

private:
	void InitRHI() override;

	TArray<int32> VertexMap;
};

#if WITH_EDITORONLY_DATA
struct MLDEFORMER_API FMLDeformerMeshMapping
{
	int32 MeshIndex = INDEX_NONE;	// The imported model's mesh info index.
	int32 TrackIndex = INDEX_NONE;	// The geom cache track that this mesh is mapped to.
	TArray<int32> SkelMeshToTrackVertexMap;	// This maps imported model individual meshes to the geomcache track's mesh data.
	TArray<int32> ImportedVertexToRenderVertexMap; // Map the imported dcc vertex number to a render vertex. This is just one of the duplicates, which shares the same position.
};
#endif

/**
 * The machine learning deformer asset.
 * At runtime this contains only the data needed to run the neural network inference.
 * In the editor it contains the skeletal mesh and geometry cache that are required to calculate vertex position deltas.
 */
UCLASS(BlueprintType, hidecategories=Object)
class MLDEFORMER_API UMLDeformerAsset : public UObject
{
	GENERATED_BODY()

public:
	UMLDeformerAsset(const FObjectInitializer& ObjectInitializer);

	//~UObject interface
	virtual void Serialize(FArchive& Archive) override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End of UObject interface

#if WITH_EDITOR
	FText GetGeomCacheErrorText(UGeometryCache* InGeomCache) const;
	FText GetAnimSequenceErrorText(UGeometryCache* InGeomCache, UAnimSequence* InAnimSequence) const;
	FText GetVertexErrorText(USkeletalMesh* InSkelMesh, UGeometryCache* InGeomCache, const FText& SkelName, const FText& GeomCacheName) const;
	FText GetBaseAssetChangedErrorText() const;
	FText GetTargetAssetChangedErrorText() const;
	FText GetInputsErrorText() const;
	FText GetIncompatibleSkeletonErrorText(USkeletalMesh* InSkelMesh, UAnimSequence* InAnimSeq) const;
	FText GetSkeletalMeshNeedsReimportErrorText() const;
	FText GetMeshMappingErrorText() const;

	static int32 ExtractNumImportedGeomCacheVertices(UGeometryCache* GeomCache);
	static int32 ExtractNumImportedSkinnedVertices(USkeletalMesh* SkeletalMesh);
	static void GenerateMeshMappings(UMLDeformerAsset* DeformerAsset, TArray<FMLDeformerMeshMapping>& OutMeshMappings, TArray<FString>& OutFailedImportedMeshNames);

	void UpdateCachedNumVertices();
	bool IsCompatibleWithNeuralNet() const;
	FMLDeformerInputInfo CreateInputInfo() const;
#endif

	void InitVertexMap();
	void InitGPUData();

	const TArray<int32>& GetVertexMap() const { return VertexMap; }
	UNeuralNetwork* GetInferenceNeuralNetwork() const { return NeuralNetwork.Get(); }
	const FVertexMapBuffer& GetVertexMapBuffer() const { return VertexMapBuffer; }
	const FVector3f& GetVertexDeltaMean() const { return VertexDeltaMean; }
	const FVector3f& GetVertexDeltaScale() const { return VertexDeltaScale; }
	const FMLDeformerInputInfo& GetInputInfo() const { return InputInfo; }
	FMLDeformerInputInfo& GetInputInfo() { return InputInfo; }
	ETrainingInputs GetTrainingInputs() const { return TrainingInputs; }
	ETrainingInputs GetTempTrainingInputs() const { return TempTrainingInputs; }
	void SetTempTrainingInputs(ETrainingInputs Inputs) { TempTrainingInputs = Inputs; }
	void SetInputInfo(const FMLDeformerInputInfo& Input) { InputInfo = Input; }

#if WITH_EDITORONLY_DATA
	const FTransform& GetAlignmentTransform() const { return AlignmentTransform; }

	const USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh;  }
	USkeletalMesh* GetSkeletalMesh() { return SkeletalMesh; }

	const UGeometryCache* GetGeometryCache() const { return GeometryCache; }
	UGeometryCache* GetGeometryCache() { return GeometryCache; }

	const UAnimSequence* GetAnimSequence() const { return AnimSequence;  }
	UAnimSequence* GetAnimSequence() { return AnimSequence; }

	const UMLDeformerVizSettings* GetVizSettings() const { return VizSettings; }
	UMLDeformerVizSettings* GetVizSettings() { return VizSettings; }

	int32 GetNumFrames() const;
	int32 GetTrainingFrameLimit() const { return MaxTrainingFrames; }
	int32 GetNumFramesForTraining() const;
	int32 GetCacheSizeInMegabytes() const { return CacheSizeInMegabytes; }
	float GetDeltaCutoffLength() const { return DeltaCutoffLength; }
#endif

public:
	/** The neural network to use for inference. */
	UPROPERTY()
	TObjectPtr<UNeuralNetwork> NeuralNetwork = nullptr;

	/** This is an index per vertex in the mesh, indicating the imported vertex number from the source asset. */
	UPROPERTY()
	TArray<int32> VertexMap;

	/** The vertex delta mean. This is passed to the deformer shader to reconstruct the correct output deltas. */
	UPROPERTY()
	FVector3f VertexDeltaMean = FVector3f::ZeroVector;

	/** The vertex delta scale. This is passed to the deformer shader to rescale the output deltas. */
	UPROPERTY()
	FVector3f VertexDeltaScale = FVector3f::OneVector;

	/** Cached number of skeltal mesh vertices. */
	UPROPERTY()
	int32 NumSkeletalMeshVerts = 0;

	/** Cached number of geom cache vertices. */
	UPROPERTY()
	int32 NumGeomCacheVerts = 0;

	/** 
	 * The information about the neural network inputs. This contains things such as bone names, morph target names, etc. 
	 * It also describes the order of inputs to the network.
	 */
	UPROPERTY()
	FMLDeformerInputInfo InputInfo;

	/** Describes what inputs we should train the neural network on. */
	UPROPERTY(EditAnywhere, Category = "Inputs and Output")
	ETrainingInputs TrainingInputs = ETrainingInputs::BonesAndCurves;
	ETrainingInputs TempTrainingInputs = ETrainingInputs::BonesAndCurves;	// Work around a bug in Slate related to combobox changes.

	/** GPU buffers for Vertex Map. */
	FVertexMapBuffer VertexMapBuffer;

	/** Fence used in render thread cleanup on destruction. */
	FRenderCommandFence RenderResourceDestroyFence;

#if WITH_EDITORONLY_DATA
	/** The skeletal mesh that represents the linear skinned mesh. */
	UPROPERTY(EditAnywhere, Category = "Base Mesh")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/** The geometry cache that represents the complex mesh deformations. */
	UPROPERTY(EditAnywhere, Category = "Target Mesh")
	TObjectPtr<UGeometryCache> GeometryCache = nullptr;

	/**
	 * The animation sequence to apply to the base mesh. This has to match the animation of the target mesh's geometry cache. 
	 * Internally we force the Interpolation property for this motion to be "Step".
	 */
	UPROPERTY(EditAnywhere, Category = "Base Mesh")
	TObjectPtr<UAnimSequence> AnimSequence = nullptr;

	/** The visualization settings. */
	UPROPERTY()
	TObjectPtr<UMLDeformerVizSettings> VizSettings = nullptr;

	/** The number of hidden layers that the neural network model will have.\nHigher numbers will slow down performance but can deal with more complex deformations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1", ClampMax = "10"))
	int32 NumHiddenLayers = 2;

	/** The number of units/neurons per hidden layer. Higher numbers will slow down performance but allow for more complex mesh deformations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 NumNeuronsPerLayer = 256;

	/** The number of frames per batch when training the model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 BatchSize = 128;

	/** The number of epochs to process without any decay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 Epochs = 10;

	/** The number of epochs to process that include a decay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 EpochsWithDecay = 15;

	/** The maximum numer of training frames (samples) to train on. Use this to train on a sub-section of your full training data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = "1"))
	int32 MaxTrainingFrames = 1000000;

	/** The maximum allowed size of the training cache in memory, in megabytes. So a value of 1024 would be one gigabyte. The larger the cache size the faster the training. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Settings", meta = (ClampMin = 0))
	int32 CacheSizeInMegabytes = 4096;	// 4 Gigabyte

	/** The learning rate used during the model training. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "0.000001", ClampMax = "1.0"))
	float LearningRate = 0.00175f;

	/** The decay function to adapt the learning rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings")
	EDecayFunction DecayFunction = EDecayFunction::Multiplicative;

	/** The decay rate to apply to the learning rate once non-decay epochs have been reached. Higher values give less decay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float DecayRate = 0.95f;

	/** The activation function to use in the neural network. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings")
	EActivationFunction ActivationFunction = EActivationFunction::LRelu;

	/** The loss function to use during model training. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings")
	ELossFunction LossFunction = ELossFunction::L1;

	/** Shrinkage speed. Only if the shrinkage loss is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (EditCondition = "LossFunction == ELossFunction::Shrinkage", ClampMin = "1.0", ClampMax = "1000.0"))
	float ShrinkageSpeed = 10.0f;

	/** Shrinkage threshold. Only if the shrinkage loss is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (EditCondition = "LossFunction == ELossFunction::Shrinkage", ClampMin = "0.0"))
	float ShrinkageThreshold = 0.1f;

	/** The percentage of noise to add. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings", meta = (ClampMin = "0.0", ClampMax = "100.0")) // A percentage.
	float NoiseAmount = 0.5;

	/** The loss function to use during model training. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Training Settings")
	EDeviceType DeviceType = EDeviceType::GPU;

	/** The transform that aligns the Geometry Cache to the SkeletalMesh. This will mostly apply some scale and a rotation, but no translation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Mesh")
	FTransform AlignmentTransform = FTransform::Identity;

	/** Sometimes there can be some vertices that cause some issues that cause deltas to be very long. We can ignore these deltas by setting a cutoff value. 
	  * Deltas that are longer than the cutoff value (in units), will be ignored and set to zero length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delta Generation", meta = (ClampMin = "0.01"))
	float DeltaCutoffLength = 30.0f;
#endif // WITH_EDITORONLY_DATA
};
