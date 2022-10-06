// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Templates/SharedPointer.h"

// TODO:
// - Transition Sim LOD data
// - Move wrap deformer into struct similar to FMeshToMeshVertData
// - Add FVertexBoneData to MANAGED_ARRAY_TYPE

namespace UE::Chaos::ClothAsset
{
	// TODO: Do we want to move the skinning data away from the static render data?
	///**
	// * Skinning data per vertex
	// */
	//struct FVertexBoneData
	//{
	//	static const int8 MaxTotalInfluences = 12;  // Up to MAX_TOTAL_INFLUENCES bone indices that this vert is weighted to
	//
	//	int32 NumInfluences;
	//	uint16 BoneIndices[MaxTotalInfluences];
	//	float BoneWeights[MaxTotalInfluences];
	//};

	/**
	 * Tailored Cloth Asset Collection containing draping and pattern information.
	 */
	class CHAOSCLOTHASSET_API FClothCollection : public FManagedArrayCollection
	{
	public:
		typedef FManagedArrayCollection Super;

		FClothCollection();
		FClothCollection(FClothCollection&) = delete;
		FClothCollection& operator=(const FClothCollection&) = delete;
		FClothCollection(FClothCollection&&) = default;
		FClothCollection& operator=(FClothCollection&&) = default;

		using Super::Serialize;
		void Serialize(FArchive& Ar);

		// Attribute groups, predefined data member of the FClothLod object.
		static const FName SimVerticesGroup;  // Contains patterns' 2D positions, 3D draped position (rest)
		static const FName SimFacesGroup;  // Contains indices to sim vertex
		static const FName RenderVerticesGroup;  // Contains pattern's 3D render model
		static const FName RenderFacesGroup;  // Contains indices to render vertex
		static const FName WrapDeformersGroup;  // Contains the wrap deformers cloth capture information
		static const FName PatternsGroup;  // Contains pattern relationships to other groups
		static const FName StitchingsGroup;  // Contains stitched sim vertex indices for quick welding or constraint creations
		static const FName TethersGroup;  // Tethers information
		static const FName TetherBatchesGroup;  // Tethers parallel batch processing information
		static const FName LodsGroup;  // Lod split information

		// Sim Vertices Group
		TManagedArray<FVector2f> SimPosition;
		TManagedArray<FVector3f> SimRestPosition;
		TManagedArray<FVector3f> SimRestNormal;  // Used for capture, maxdistance, backstop authoring ...etc
		//TManagedArray<FVertexBoneData> SimBoneData;

		// Sim Faces Group
		TManagedArray<FIntVector3> SimIndices;  // The indices point to the elements in the Sim Vertices arrays but don't include the LOD start offset

		// Render Vertices Group
		TManagedArray<FVector3f> RenderPosition;
		TManagedArray<FVector3f> RenderNormal;
		TManagedArray<FVector3f> RenderTangentU;
		TManagedArray<FVector3f> RenderTangentV;
		TManagedArray<TArray<FVector2f>> RenderUVs;
		TManagedArray<FLinearColor> RenderColor;
		//TManagedArray<FVertexBoneData> RenderBoneData;

		// Render Faces Group
		TManagedArray<FIntVector3> RenderIndices;  // The indices point to the elements in the Render Vertices arrays but don't include the LOD start offset
		TManagedArray<int32> RenderMaterialIndex;  // Render material per triangle

		// TODO: FMeshToMeshVertData
		//// Wrap Deformers Group (render mesh captured, sim mesh capturing)
		//TManagedArray<FVector4f> CapturedPosition;  // Barycentric coordinate and distance for captured render positions
		//TManagedArray<FVector4f> CapturedNormal;  // Barycentric coordinate and distance for captured render normals
		//TManagedArray<FVector4f> CapturedTangent;  // Barycentric coordinate and distance for captured render tangents
		//TManagedArray<int32> CapturingFace;  // Sim triangle index
		//TManagedArray<float> CapturingWeight;  // Weight for when using multiple capture influence per point, 1.0 otherwise

		// Patterns Group
		TManagedArray<int32> SimVerticesStart;
		TManagedArray<int32> SimVerticesEnd;
		TManagedArray<int32> SimFacesStart;
		TManagedArray<int32> SimFacesEnd;
		TManagedArray<int32> RenderVerticesStart;
		TManagedArray<int32> RenderVerticesEnd;
		TManagedArray<int32> RenderFacesStart;
		TManagedArray<int32> RenderFacesEnd;
		TManagedArray<int32> WrapDeformerStart;
		TManagedArray<int32> WrapDeformerEnd;
		TManagedArray<int32> NumWeights;  // Number of weights stored between WrapDeformerStart and WrapDeformerEnd
		TManagedArray<int32> StatusFlags;  // Whether this pattern should be rendered, simulated, ...etc
		TManagedArray<int32> SimMaterialIndex;  // Cloth material per pattern

		// Stitching Group
		TManagedArray<TSet<int32>> StitchedVertices;  // Stitched vertex indices

		// Tethers Group
		TManagedArray<int32> TetherKinematicIndex;
		TManagedArray<int32> TetherDynamicIndex;
		TManagedArray<float> TetherReferenceLength;

		// Tether Batches Group
		TManagedArray<int32> TetherStart;
		TManagedArray<int32> TetherEnd;

		// LOD Group
		TManagedArray<int32> PatternStart;
		TManagedArray<int32> PatternEnd;
		TManagedArray<int32> StitchingStart;
		TManagedArray<int32> StitchingEnd;
		TManagedArray<int32> TetherBatchStart;
		TManagedArray<int32> TetherBatchEnd;
		TManagedArray<int32> LodBiasDepth;  // The number of LODs covered by each Sim LOD (for the wrap deformer)

	protected:
		void Construct();
	};
}  // End namespace UE::Chaos::ClothAsset
