// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MLDeformerInputInfo.h"
#include "UObject/ObjectPtr.h"
#include "UObject/Object.h"
#include "Templates/UniquePtr.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "BoneContainer.h"
#include "RenderCommandFence.h"
#include "RenderResource.h"
#include "MLDeformerCurveReference.h"
#include "MLDeformerModel.generated.h"

class UMLDeformerAsset;
class UMLDeformerVizSettings;
class UMLDeformerModelInstance;
class UMLDeformerComponent;
class UAnimSequence;
class UGeometryCache;
class UNeuralNetwork;

namespace UE::MLDeformer
{
	/** The vertex map on the GPU. */
	class FVertexMapBuffer : public FVertexBufferWithSRV
	{
	public:
		void Init(const TArray<int32>& InVertexMap)	{ VertexMap = InVertexMap; }

	private:
		void InitRHI() override;
		TArray<int32> VertexMap;
	};
}	// namespace UE::MLDeformer

/** The training inputs. Specifies what data to include in training. */
UENUM()
enum class EMLDeformerTrainingInputFilter : uint8
{
	BonesAndCurves = 0, /** Include both bone rotations and curve values. */
	BonesOnly,			/** Include only bone rotations. */
	CurvesOnly			/** Include only curve values. */
};

DECLARE_DELEGATE_OneParam(FMLDeformerModelOnPostEditProperty, FPropertyChangedEvent&)

/**
 * The ML Deformer runtime model base class.
 * All models should be inherited from this class.
 **/
UCLASS(Abstract)
class MLDEFORMERFRAMEWORK_API UMLDeformerModel 
	: public UObject	
	, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:
	virtual ~UMLDeformerModel() = default;

	// Methods you might want to override.
	virtual void Init(UMLDeformerAsset* InDeformerAsset);
	virtual void InitGPUData();
	virtual UMLDeformerInputInfo* CreateInputInfo();
	virtual UMLDeformerModelInstance* CreateModelInstance(UMLDeformerComponent* Component);
	virtual FString GetDisplayName() const PURE_VIRTUAL(UMLDeformerModel::GetDisplayName, return FString(););
	virtual void PostMLDeformerComponentInit(UMLDeformerModelInstance* ModelInstance) {}
	virtual bool IsNeuralNetworkOnGPU() const { return true; }	// GPU neural network.
#if WITH_EDITORONLY_DATA
	virtual bool HasTrainingGroundTruth() const { return false; }
	virtual void SampleGroundTruthPositions(float SampleTime, TArray<FVector3f>& OutPositions) {}
#endif
#if WITH_EDITOR
	virtual void UpdateNumBaseMeshVertices();
	virtual void UpdateCachedNumVertices();
	virtual void UpdateNumTargetMeshVertices() PURE_VIRTUAL(UMLDeformerModel::GetNumTargetMeshVertices; mTargetMeshVerts = 0;);
	static int32 ExtractNumImportedSkinnedVertices(const USkeletalMesh* SkeletalMesh);
#endif

	// UObject overrides.
	virtual void Serialize(FArchive& Archive) override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;
	// ~END UObject overrides.

	// IBoneReferenceSkeletonProvider overrides.
	virtual USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;
	// ~END IBoneReferenceSkeletonProvider overrides.

	UMLDeformerAsset* GetDeformerAsset() const;
	EMLDeformerTrainingInputFilter GetTrainingInputs() const { return TrainingInputs; }
	UMLDeformerInputInfo* GetInputInfo() const { return InputInfo.Get(); }

	int32 GetNumBaseMeshVerts() const { return NumBaseMeshVerts; }
	int32 GetNumTargetMeshVerts() const { return NumTargetMeshVerts; }

	const TArray<int32>& GetVertexMap() const { return VertexMap; }
	const UE::MLDeformer::FVertexMapBuffer& GetVertexMapBuffer() const { return VertexMapBuffer; }

	UNeuralNetwork* GetNeuralNetwork() const { return NeuralNetwork.Get(); }
	void SetNeuralNetwork(UNeuralNetwork* InNeuralNetwork);

#if WITH_EDITORONLY_DATA
	// UObject overrides.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// ~END UObject overrides.

	void InitVertexMap();

	FMLDeformerModelOnPostEditProperty& OnPostEditChangeProperty() { return PostEditPropertyEvent; }

	UMLDeformerVizSettings* GetVizSettings() const { return VizSettings; }
	const USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh;  }
	USkeletalMesh* GetSkeletalMesh() { return SkeletalMesh; }
	const UAnimSequence* GetAnimSequence() const { return AnimSequence;  }
	UAnimSequence* GetAnimSequence() { return AnimSequence; }
	int32 GetTrainingFrameLimit() const { return MaxTrainingFrames; }
	const FTransform& GetAlignmentTransform() const { return AlignmentTransform; }
	TArray<FBoneReference>& GetBoneIncludeList() { return BoneIncludeList; }
	const TArray<FBoneReference>& GetBoneIncludeList() const { return BoneIncludeList; }
	TArray<FMLDeformerCurveReference>& GetCurveIncludeList() { return CurveIncludeList; }
	const TArray<FMLDeformerCurveReference>& GetCurveIncludeList() const { return CurveIncludeList; }
	float GetDeltaCutoffLength() const { return DeltaCutoffLength; }
#endif	// #if WITH_EDITORONLY_DATA

protected:
	TObjectPtr<UMLDeformerAsset> DeformerAsset = nullptr;
	FMLDeformerModelOnPostEditProperty PostEditPropertyEvent;

	void SetInputInfo(UMLDeformerInputInfo* Input) { InputInfo = Input; }
	void FloatArrayToVector3Array(const TArray<float>& FloatArray, TArray<FVector3f>& OutVectorArray);

public:
	/** Cached number of skeltal mesh vertices. */
	UPROPERTY()
	int32 NumBaseMeshVerts = 0;

	/** Cached number of target mesh vertices. */
	UPROPERTY()
	int32 NumTargetMeshVerts = 0;

	/** Describes what inputs we should train the neural network on. */
	UPROPERTY(EditAnywhere, Category = "Inputs and Output")
	EMLDeformerTrainingInputFilter TrainingInputs = EMLDeformerTrainingInputFilter::BonesOnly;

	/** 
	 * The information about the neural network inputs. This contains things such as bone names and curve names.
	 */
	UPROPERTY()
	TObjectPtr<UMLDeformerInputInfo> InputInfo = nullptr;

	/** This is an index per vertex in the mesh, indicating the imported vertex number from the source asset. */
	UPROPERTY()
	TArray<int32> VertexMap;

	/** The neural network that is used during inference. */
	UPROPERTY()
	TObjectPtr<UNeuralNetwork> NeuralNetwork = nullptr;

	/** GPU buffers for Vertex Map. */
	UE::MLDeformer::FVertexMapBuffer VertexMapBuffer;

	/** Fence used in render thread cleanup on destruction. */
	FRenderCommandFence RenderResourceDestroyFence;

	/** Delegate that will be called immediately before the NeuralNetwork is changed. */
	DECLARE_MULTICAST_DELEGATE(FNeuralNetworkModifyDelegate);
	FNeuralNetworkModifyDelegate NeuralNetworkModifyDelegate;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UMLDeformerVizSettings> VizSettings = nullptr;

	/** The skeletal mesh that represents the linear skinned mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Mesh")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/**
	 * The animation sequence to apply to the base mesh. This has to match the animation of the target mesh's geometry cache. 
	 * Internally we force the Interpolation property for this motion to be "Step".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Mesh")
	TObjectPtr<UAnimSequence> AnimSequence = nullptr;

	/** The transform that aligns the Geometry Cache to the SkeletalMesh. This will mostly apply some scale and a rotation, but no translation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Mesh")
	FTransform AlignmentTransform = FTransform::Identity;

	/** The bones to include during training. When none are provided, all bones of the Skeleton will be included. */
	UPROPERTY(EditAnywhere, Category = "Inputs and Output", meta = (EditCondition = "TrainingInputs == EMLDeformerTrainingInputFilter::BonesAndCurves || TrainingInputs == EMLDeformerTrainingInputFilter::BonesOnly"))
	TArray<FBoneReference> BoneIncludeList;

	/** The curves to include during training. When none are provided, all curves of the Skeleton will be included. */
	UPROPERTY(EditAnywhere, Category = "Inputs and Output", meta = (EditCondition = "TrainingInputs == EMLDeformerTrainingInputFilter::BonesAndCurves || TrainingInputs == EMLDeformerTrainingInputFilter::CurvesOnly"))
	TArray<FMLDeformerCurveReference> CurveIncludeList;

	/** The maximum numer of training frames (samples) to train on. Use this to train on a sub-section of your full training data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inputs and Output", meta = (ClampMin = "1"))
	int32 MaxTrainingFrames = 1000000;

	/** Sometimes there can be some vertices that cause some issues that cause deltas to be very long. We can ignore these deltas by setting a cutoff value. 
	  * Deltas that are longer than the cutoff value (in units), will be ignored and set to zero length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inputs and Output", meta = (ClampMin = "0.01", ForceUnits="cm"))
	float DeltaCutoffLength = 30.0f;
#endif
};
