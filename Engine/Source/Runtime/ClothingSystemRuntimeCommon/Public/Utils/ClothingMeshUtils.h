// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SkeletalMeshTypes.h"
#include "Chaos/AABBTree.h"
#include "Chaos/GeometryParticlesfwd.h"

DECLARE_LOG_CATEGORY_EXTERN(LogClothingMeshUtils, Log, All);

struct FClothPhysicalMeshData;
struct FPointWeightMap;

namespace ClothingMeshUtils
{
	struct CLOTHINGSYSTEMRUNTIMECOMMON_API ClothMeshDesc
	{
		struct FClothBvEntry
		{
			ClothMeshDesc* TmData;
			int32 Index;

			bool HasBoundingBox() const { return true; }

			Chaos::FAABB3 BoundingBox() const
			{
				int32 TriBaseIdx = Index * 3;

				const uint32 IA = TmData->Indices[TriBaseIdx + 0];
				const uint32 IB = TmData->Indices[TriBaseIdx + 1];
				const uint32 IC = TmData->Indices[TriBaseIdx + 2];

				const FVector3f& A = TmData->Positions[IA];
				const FVector3f& B = TmData->Positions[IB];
				const FVector3f& C = TmData->Positions[IC];

				Chaos::FAABB3 Bounds(A, A);

				Bounds.GrowToInclude(B);
				Bounds.GrowToInclude(C);

				return Bounds;
			}

			template<typename TPayloadType>
			int32 GetPayload(int32 Idx) const
			{
				return Idx;
			}

			Chaos::FUniqueIdx UniqueIdx() const
			{
				return Chaos::FUniqueIdx(Index);
			}
		};

		ClothMeshDesc(TArrayView<const FVector3f> InPositions, TArrayView<const FVector3f> InNormals, TArrayView<const uint32> InIndices)
			: Positions(InPositions)
			, Normals(InNormals)
			, Indices(InIndices)
			, bHasValidBVH(false)
		{
		}

		bool HasValidMesh() const
		{
			return Positions.Num() == Normals.Num() && Indices.Num() % 3 == 0;
		}

		TArray<int32> FindCandidateTriangles(const FVector& InPoint, float InTolerance = KINDA_SMALL_NUMBER);

		TArrayView<const FVector3f> Positions;
		TArrayView<const FVector3f> Normals;
		TArrayView<const uint32> Indices;

		bool bHasValidBVH;
		Chaos::TAABBTree<int32, Chaos::TAABBTreeLeafArray<int32, false>, false> BVH;
	};

	// Static method for calculating a skinned mesh result from source data
	// The bInPlaceOutput allows us to directly populate arrays that are already allocated
	// bRemoveScaleAndInvertPostTransform will determine if the PostTransform should be inverted and the scale removed (NvCloth uses this). It is templated to remove branches at compile time
	template<bool bInPlaceOutput = false, bool bRemoveScaleAndInvertPostTransform = true>
	void CLOTHINGSYSTEMRUNTIMECOMMON_API
	SkinPhysicsMesh(
		const TArray<int32>& BoneMap, // UClothingAssetCommon::UsedBoneIndices
		const FClothPhysicalMeshData& InMesh, 
		const FTransform& PostTransform, // Final transform to apply to component space positions and normals
		const FMatrix44f* InBoneMatrices, 
		const int32 InNumBoneMatrices, 
		TArray<FVector3f>& OutPositions, 
		TArray<FVector3f>& OutNormals,
		uint32 ArrayOffset = 0); // Used for Chaos Cloth


	/**
	 * Compute the max edge length for each edge coming out of a mesh vertex. Useful for guiding the search radius when
	 * searching for nearest triangles.
	 */
	inline void CLOTHINGSYSTEMRUNTIMECOMMON_API ComputeMaxEdgeLength(const ClothMeshDesc& TargetMesh, 
																	 TArray<float>& OutMaxEdgeLength)
	{
		const int32 NumMesh0Verts = TargetMesh.Positions.Num();

		// Check we have properly formed triangles
		ensure(TargetMesh.Indices.Num() % 3 == 0);
		const int32 NumMesh0Tris = TargetMesh.Indices.Num() / 3;

		OutMaxEdgeLength.Init(0.0f, NumMesh0Verts);

		for (int32 TriangleIdx = 0; TriangleIdx < NumMesh0Tris; ++TriangleIdx)
		{
			const uint32* Triangle = &TargetMesh.Indices[TriangleIdx * 3];

			for (int32 Vertex0Idx = 0; Vertex0Idx < 3; ++Vertex0Idx)
			{
				const int32 Vertex1Idx = (Vertex0Idx + 1) % 3;

				const FVector& P0 = TargetMesh.Positions[Triangle[Vertex0Idx]];
				const FVector& P1 = TargetMesh.Positions[Triangle[Vertex1Idx]];

				const float EdgeLength = FVector::Distance(P0, P1);
				OutMaxEdgeLength[Triangle[Vertex0Idx]] = FMath::Max(OutMaxEdgeLength[Triangle[Vertex0Idx]], EdgeLength);
				OutMaxEdgeLength[Triangle[Vertex1Idx]] = FMath::Max(OutMaxEdgeLength[Triangle[Vertex1Idx]], EdgeLength);
			}
		}
	}

	/**
	* Given mesh information for two meshes, generate a list of skinning data to embed TargetMesh in SourceMesh
	* 
	* @param OutSkinningData            - Final skinning data
	* @param TargetMesh                 - Mesh data for the mesh we are embedding
	* @param TargetTangents             - Optional Tangents for the mesh we are embedding
	* @param SourceMesh                 - Mesh data for the mesh we are embedding into
	* @param TargetMaxEdgeLength        - Per-vertex longest incident edge length, as returned by ComputeMaxEdgeLength()
	* @param bUseMultipleInfluences     - Whether to take a weighted average of influences from multiple source triangles
	* @param KernelMaxDistance          - Max distance parameter for weighting kernel
	*/
	void CLOTHINGSYSTEMRUNTIMECOMMON_API GenerateMeshToMeshSkinningData(
		TArray<FMeshToMeshVertData>& OutSkinningData,
		const ClothMeshDesc& TargetMesh,
		const TArray<FVector3f>* TargetTangents,
		const ClothMeshDesc& SourceMesh,
		const TArray<float>& TargetMaxEdgeLength,
		bool bUseMultipleInfluences,
		float KernelMaxDistance);

	/** 
	 * Embeds a list of positions into a source mesh
	 * @param SourceMesh The mesh to embed in
	 * @param Positions The positions to embed in SourceMesh
	 * @param OutEmbeddedPositions Embedded version of the original positions, a barycentric coordinate and distance along the normal of the triangle
	 * @param OutSourceIndices Source index list for the embedded positions, 3 per position to denote the source triangle
	 */
	void CLOTHINGSYSTEMRUNTIMECOMMON_API GenerateEmbeddedPositions(
		const ClothMeshDesc& SourceMesh, 
		TArrayView<const FVector3f> Positions, 
		TArray<FVector4>& OutEmbeddedPositions, 
		TArray<int32>& OutSourceIndices);

	/**
	 * Computes how much each vertex contributes to the final mesh. The final mesh is a blend
	 * between the cloth and the skinned mesh.
	 */
	void CLOTHINGSYSTEMRUNTIMECOMMON_API ComputeVertexContributions(
		TArray<FMeshToMeshVertData> &InOutSkinningData,
		const FPointWeightMap* const InMaxDistances,
		const bool bInSmoothTransition );
	
	/**
	 * Identify vertices that are not influenced by any triangles, and compute a new single attachment for the
	 * vertex.
	 */
	void CLOTHINGSYSTEMRUNTIMECOMMON_API FixZeroWeightVertices(TArray<FMeshToMeshVertData>& InOutSkinningData,
															   const ClothMeshDesc& TargetMesh,
															   const TArray<FVector3f>* TargetTangents,
															   const ClothMeshDesc& SourceMesh,
															   const TArray<float>& TargetMaxEdgeLength );


	/**
	* Given a triangle ABC with normals at each vertex NA, NB and NC, get a barycentric coordinate
	* and corresponding distance from the triangle encoded in an FVector4 where the components are
	* (BaryX, BaryY, BaryZ, Dist)
	* @param A		- Position of triangle vertex A
	* @param B		- Position of triangle vertex B
	* @param C		- Position of triangle vertex C
	* @param NA	- Normal at vertex A
	* @param NB	- Normal at vertex B
	* @param NC	- Normal at vertex C
	* @param Point	- Point to calculate Bary+Dist for
	*/
	FVector4 GetPointBaryAndDist(
		const FVector3f& A,
		const FVector3f& B,
		const FVector3f& C,
		const FVector3f& Point);

	/**
	* Given a triangle ABC with normals at each vertex NA, NB and NC, get a barycentric coordinate
	* and corresponding distance from the triangle encoded in an FVector4 where the components are
	* (BaryX, BaryY, BaryZ, Dist)
	* @param A		- Position of triangle vertex A
	* @param B		- Position of triangle vertex B
	* @param C		- Position of triangle vertex C
	* @param NA	- Normal at vertex A
	* @param NB	- Normal at vertex B
	* @param NC	- Normal at vertex C
	* @param Point	- Point to calculate Bary+Dist for
	*/
	FVector4 GetPointBaryAndDistWithNormals(
		const FVector3f& A,
		const FVector3f& B,
		const FVector3f& C,
		const FVector3f& NA,
		const FVector3f& NB,
		const FVector3f& NC,
		const FVector3f& Point);

	/** 
	 * Object used to map vertex parameters between two meshes using the
	 * same barycentric mesh to mesh mapping data we use for clothing
	 */
	class CLOTHINGSYSTEMRUNTIMECOMMON_API FVertexParameterMapper
	{
	public:
		FVertexParameterMapper() = delete;
		FVertexParameterMapper(const FVertexParameterMapper& Other) = delete;

		FVertexParameterMapper(TArrayView<const FVector3f> InMesh0Positions,
			TArrayView<const FVector3f> InMesh0Normals,
			TArrayView<const FVector3f> InMesh1Positions,
			TArrayView<const FVector3f> InMesh1Normals,
			TArrayView<const uint32> InMesh1Indices)
			: Mesh0Positions(InMesh0Positions)
			, Mesh0Normals(InMesh0Normals)
			, Mesh1Positions(InMesh1Positions)
			, Mesh1Normals(InMesh1Normals)
			, Mesh1Indices(InMesh1Indices)
		{

		}

		/** Generic mapping function, can be used to map any type with a provided callable */
		template<typename T, typename Lambda>
		void Map(TArrayView<const T>& SourceData, TArray<T>& DestData, const Lambda& Func)
		{
			// Enforce the interp func signature (returns T and takes a bary and 3 Ts)
			// If you hit this then either the return type isn't T or your arguments aren't convertible to T
			static_assert(TAreTypesEqual<T, typename TDecay<decltype(Func(DeclVal<FVector3f>(), DeclVal<T>(), DeclVal<T>(), DeclVal<T>()))>::Type>::Value, "Invalid Lambda signature passed to Map");

			const int32 NumMesh0Positions = Mesh0Positions.Num();
			const int32 NumMesh0Normals = Mesh0Normals.Num();

			const int32 NumMesh1Positions = Mesh1Positions.Num();
			const int32 NumMesh1Normals = Mesh1Normals.Num();
			const int32 NumMesh1Indices = Mesh1Indices.Num();

			// Validate mesh data
			check(NumMesh0Positions == NumMesh0Normals);
			check(NumMesh1Positions == NumMesh1Normals);
			check(NumMesh1Indices % 3 == 0);
			check(SourceData.Num() == NumMesh1Positions);

			if(DestData.Num() != NumMesh0Positions)
			{
				DestData.Reset();
				DestData.AddUninitialized(NumMesh0Positions);
			}

			ClothMeshDesc SourceMeshDesc(Mesh1Positions, Mesh1Normals, Mesh1Indices);

			TArray<FVector4> EmbeddedPositions;
			TArray<int32> SourceIndices;
			ClothingMeshUtils::GenerateEmbeddedPositions(SourceMeshDesc, Mesh0Positions, EmbeddedPositions, SourceIndices);

			for(int32 DestVertIndex = 0 ; DestVertIndex < NumMesh0Positions ; ++DestVertIndex)
			{
				// Truncate the distance from the position data
				FVector Bary = EmbeddedPositions[DestVertIndex];

				const int32 SourceTriBaseIdx = DestVertIndex * 3;
				T A = SourceData[SourceIndices[SourceTriBaseIdx + 0]];
				T B = SourceData[SourceIndices[SourceTriBaseIdx + 1]];
				T C = SourceData[SourceIndices[SourceTriBaseIdx + 2]];

				T& DestVal = DestData[DestVertIndex];

				// If we're super close to a vertex just take it's value.
				// Otherwise call the provided interp lambda
				const static FVector OneVec(1.0f, 1.0f, 1.0f);
				FVector DiffVec = OneVec - Bary;
				if(FMath::Abs(DiffVec.X) <= SMALL_NUMBER)
				{
					DestVal = A;
				}
				else if(FMath::Abs(DiffVec.Y) <= SMALL_NUMBER)
				{
					DestVal = B;
				}
				else if(FMath::Abs(DiffVec.Z) <= SMALL_NUMBER)
				{
					DestVal = C;
				}
				else
				{
					DestData[DestVertIndex] = Func(Bary, A, B, C);
				}
			}
		}

		// Defined type mappings for brevity
		void Map(TArrayView<const float> Source, TArray<float>& Dest);

	private:

		TArrayView<const FVector3f> Mesh0Positions;
		TArrayView<const FVector3f> Mesh0Normals;
		TArrayView<const FVector3f> Mesh1Positions;
		TArrayView<const FVector3f> Mesh1Normals;
		TArrayView<const uint32> Mesh1Indices;
	};
}