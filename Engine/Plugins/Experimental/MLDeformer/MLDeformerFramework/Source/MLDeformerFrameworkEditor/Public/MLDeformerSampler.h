// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModel.h"
#include "UObject/ObjectPtr.h"

class UDebugSkelMeshComponent;
class UGeometryCacheComponent;
class UWorld;
class FSkeletalMeshLODRenderData;
class FSkinWeightVertexBuffer;
class AActor;

namespace UE::MLDeformer
{
	class FMLDeformerEditorModel;

	enum class EVertexDeltaSpace
	{
		PreSkinning,	/** Apply the deltas before skinning. */
		PostSkinning	/** Apply the deltas after skinning. */
	};

	/**
	 * The input data sampler.
	 * This class can sample bone rotations, curve values and vertex deltas.
	 * It does this by creating two temp actors, one with skeletal mesh component and one with geom cache component.
	 */
	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerSampler
	{
	public:
		virtual ~FMLDeformerSampler();

		/** Call Init whenever assets or other relevant settings change. */
		virtual void Init(FMLDeformerEditorModel* Model);

		/** Call this every time the frame changes. This will update all buffer contents. */
		virtual void Sample(int32 AnimFrameIndex);

		/** Get the time in seconds, at a given frame index. */
		virtual float GetTimeAtFrame(int32 InAnimFrameIndex) const { return 0.0f; }

		const TArray<FVector3f>& GetSkinnedVertexPositions() const { return SkinnedVertexPositions; }
		const TArray<FVector3f>& GetUnskinnedVertexPositions() const { return UnskinnedVertexPositions; }
		const TArray<float>& GetVertexDeltas() const { return VertexDeltas; }
		const TArray<float>& GetBoneRotations() const { return BoneRotations; }
		const TArray<float>& GetCurveValues() const { return CurveValues; }
		int32 GetNumImportedVertices() const { return NumImportedVertices; }
		int32 GetNumBones() const;
		EVertexDeltaSpace GetVertexDeltaSpace() const { return VertexDeltaSpace; }
		SIZE_T CalcMemUsagePerFrameInBytes() const;
		void SetVertexDeltaSpace(EVertexDeltaSpace DeltaSpace) { VertexDeltaSpace = DeltaSpace; }
		bool IsInitialized() const { return SkelMeshActor.Get() != nullptr; }

	protected:
		virtual void CreateActors();
		virtual void RegisterTargetComponents() {}
		void ExtractSkinnedPositions(int32 LODIndex, TArray<FMatrix44f>& InBoneMatrices, TArray<FVector3f>& TempPositions, TArray<FVector3f>& OutPositions) const;
		void ExtractUnskinnedPositions(int32 LODIndex, TArray<FVector3f>& OutPositions) const;
		FMatrix44f CalcInverseSkinningTransform(int32 VertexIndex, const FSkeletalMeshLODRenderData& SkelMeshLODData, const FSkinWeightVertexBuffer& SkinWeightBuffer) const;
		AActor* CreateNewActor(UWorld* InWorld, const FName& Name) const;

		void UpdateSkeletalMeshComponent();
		void UpdateSkinnedPositions();
		void UpdateBoneRotations();
		void UpdateCurveValues();

	protected:
		/** The vertex delta editor model used to sample. */
		FMLDeformerEditorModel* EditorModel = nullptr;

		/** The skeletal mesh actor used to sample the skinned vertex positions. */
		TObjectPtr<AActor> SkelMeshActor = nullptr;

		/** The actor used for the target mesh. */
		TObjectPtr<AActor> TargetMeshActor = nullptr;

		/** The vertex delta model associated with this sampler. */
		TObjectPtr<UMLDeformerModel> Model = nullptr;

		/** The skeletal mesh component used to sample skinned positions. */
		TObjectPtr<UDebugSkelMeshComponent>	SkeletalMeshComponent = nullptr;

		/** The skinned vertex positions. */
		TArray<FVector3f> SkinnedVertexPositions;

		/** The unskinned vertex positions. */
		TArray<FVector3f> UnskinnedVertexPositions;

		/** A temp array to store vertex positions in. */
		TArray<FVector3f> TempVertexPositions;

		/** The sampled bone matrices. */
		TArray<FMatrix44f> BoneMatrices;

		/**
		 * The vertex deltas as float buffer. The number of floats equals: NumImportedVerts * 3.
		 * The layout is (xyz)(xyz)(xyz)(...)
		 */
		TArray<float> VertexDeltas;	

		/**
		 * The bone rotation floats.
		 * The number of floats in the buffer equals to NumBones * 6.
		 * The six floats represent two columns of the 3x3 rotation matrix of the bone.
		 */
		TArray<float> BoneRotations;

		/** A float for each Skeleton animation curve. */
		TArray<float> CurveValues;	

		/** The current sample time, in seconds. */
		float SampleTime = 0.0f;

		/** The number of imported vertices of the skeletal mesh and geometry cache. This will be 8 for a cube. */
		int32 NumImportedVertices = 0;

		/** The animation frame we sampled the deltas for. */
		int32 AnimFrameIndex = -1;

		/** The vertex delta space (pre or post skinning) used when calculating the deltas. */
		EVertexDeltaSpace VertexDeltaSpace = EVertexDeltaSpace::PreSkinning;
	};
}	// namespace UE::MLDeformer
