// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/ClothingMeshUtils.h"

#include "ClothPhysicalMeshData.h"

#include "Math/UnrealMathUtility.h"
#include "Logging/LogMacros.h"
#include "Async/ParallelFor.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

DEFINE_LOG_CATEGORY(LogClothingMeshUtils)

#define LOCTEXT_NAMESPACE "ClothingMeshUtils"

// This must match NUM_INFLUENCES_PER_VERTEX in GpuSkinCacheComputeShader.usf and GpuSkinVertexFactory.ush
// TODO: Make this easier to change in without messing things up
#define NUM_INFLUENCES_PER_VERTEX 5

namespace ClothingMeshUtils
{
	// Explicit template instantiations of SkinPhysicsMesh
	template
	void CLOTHINGSYSTEMRUNTIMECOMMON_API SkinPhysicsMesh<true, false>(const TArray<int32>& BoneMap, const FClothPhysicalMeshData& InMesh, const FTransform& RootBoneTransform,
		const FMatrix44f* InBoneMatrices, const int32 InNumBoneMatrices, TArray<FVector3f>& OutPositions, TArray<FVector3f>& OutNormals, uint32 ArrayOffset);
	template
	void CLOTHINGSYSTEMRUNTIMECOMMON_API SkinPhysicsMesh<false, true>(const TArray<int32>& BoneMap, const FClothPhysicalMeshData& InMesh, const FTransform& RootBoneTransform,
		const FMatrix44f* InBoneMatrices, const int32 InNumBoneMatrices, TArray<FVector3f>& OutPositions, TArray<FVector3f>& OutNormals, uint32 ArrayOffset);

	// inline function used to force the unrolling of the skinning loop
	FORCEINLINE static void AddInfluence(FVector3f& OutPosition, FVector3f& OutNormal, const FVector3f& RefParticle, const FVector3f& RefNormal, const FMatrix44f& BoneMatrix, const float Weight)
	{
		OutPosition += BoneMatrix.TransformPosition(RefParticle) * Weight;
		OutNormal += BoneMatrix.TransformVector(RefNormal) * Weight;
	}

	template<bool bInPlaceOutput, bool bRemoveScaleAndInvertPostTransform>
	void SkinPhysicsMesh(
		const TArray<int32>& InBoneMap,
		const FClothPhysicalMeshData& InMesh,
		const FTransform& PostTransform,
		const FMatrix44f* InBoneMatrices,
		const int32 InNumBoneMatrices,
		TArray<FVector3f>& OutPositions,
		TArray<FVector3f>& OutNormals,
		uint32 ArrayOffset)
	{
		const uint32 NumVerts = InMesh.Vertices.Num();

		if(!bInPlaceOutput)
		{
			ensure(ArrayOffset == 0);
			OutPositions.Reset(NumVerts);
			OutNormals.Reset(NumVerts);
			OutPositions.AddZeroed(NumVerts);
			OutNormals.AddZeroed(NumVerts);
		}
		else
		{
			check((uint32) OutPositions.Num() >= NumVerts + ArrayOffset);
			check((uint32) OutNormals.Num() >= NumVerts + ArrayOffset);
			// PS4 performance note: It is faster to zero the memory first instead of changing this function to work with uninitialized memory
			FMemory::Memzero((uint8*)OutPositions.GetData() + ArrayOffset * sizeof(FVector3f), NumVerts * sizeof(FVector3f));
			FMemory::Memzero((uint8*)OutNormals.GetData() + ArrayOffset * sizeof(FVector3f), NumVerts * sizeof(FVector3f));
		}

		const int32 MaxInfluences = InMesh.MaxBoneWeights;
		UE_CLOG(MaxInfluences > 12, LogClothingMeshUtils, Warning, TEXT("The cloth physics mesh skinning code can't cope with more than 12 bone influences."));

		const int32* const RESTRICT BoneMap = InBoneMap.GetData();  // Remove RangeCheck for faster skinning in development builds
		const FMatrix44f* const RESTRICT BoneMatrices = InBoneMatrices;
		
		static const uint32 MinParallelVertices = 500;  // 500 seems to be the lowest threshold still giving gains even on profiled assets that are only using a small number of influences

		ParallelFor(NumVerts, [&InMesh, &PostTransform, BoneMap, BoneMatrices, &OutPositions, &OutNormals, ArrayOffset](uint32 VertIndex)
		{
			// Fixed particle, needs to be skinned
			const uint16* const RESTRICT BoneIndices = InMesh.BoneData[VertIndex].BoneIndices;
			const float* const RESTRICT BoneWeights = InMesh.BoneData[VertIndex].BoneWeights;

			// WARNING - HORRIBLE UNROLLED LOOP + JUMP TABLE BELOW
			// done this way because this is a pretty tight and perf critical loop. essentially
			// rather than checking each influence we can just jump into this switch and fall through
			// everything to compose the final skinned data
			const FVector3f& RefParticle = InMesh.Vertices[VertIndex];
			const FVector3f& RefNormal = InMesh.Normals[VertIndex];
			FVector3f& OutPosition = OutPositions[bInPlaceOutput ? VertIndex + ArrayOffset : VertIndex];
			FVector3f& OutNormal = OutNormals[bInPlaceOutput ? VertIndex + ArrayOffset : VertIndex];
			switch (InMesh.BoneData[VertIndex].NumInfluences)
			{
			case 12: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[11]]], BoneWeights[11]);  // Intentional fall through
			case 11: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[10]]], BoneWeights[10]);  // Intentional fall through
			case 10: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 9]]], BoneWeights[ 9]);  // Intentional fall through
			case  9: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 8]]], BoneWeights[ 8]);  // Intentional fall through
			case  8: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 7]]], BoneWeights[ 7]);  // Intentional fall through
			case  7: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 6]]], BoneWeights[ 6]);  // Intentional fall through
			case  6: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 5]]], BoneWeights[ 5]);  // Intentional fall through
			case  5: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 4]]], BoneWeights[ 4]);  // Intentional fall through
			case  4: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 3]]], BoneWeights[ 3]);  // Intentional fall through
			case  3: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 2]]], BoneWeights[ 2]);  // Intentional fall through
			case  2: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 1]]], BoneWeights[ 1]);  // Intentional fall through
			case  1: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 0]]], BoneWeights[ 0]);  // Intentional fall through
			default: break;
			}

			if (bRemoveScaleAndInvertPostTransform)
			{
				// Ignore any user scale. It's already accounted for in our skinning matrices
				// This is the use case for NVcloth
				FTransform PostTransformInternal = PostTransform;
				PostTransformInternal.SetScale3D(FVector(1.0f));

				OutPosition = PostTransformInternal.InverseTransformPosition(OutPosition);
				OutNormal = PostTransformInternal.InverseTransformVector(OutNormal);
			}
			else
			{
				OutPosition = PostTransform.TransformPosition(OutPosition);
				OutNormal = PostTransform.TransformVector(OutNormal);
			}

			if (OutNormal.SizeSquared() > SMALL_NUMBER)
			{
				OutNormal = OutNormal.GetUnsafeNormal();
			}
		}, NumVerts > MinParallelVertices ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
	}

	/** 
	 * Gets the best match triangle for a specified position from the triangles in Mesh.
	 * Performs no validation on the incoming mesh data, the mesh data should be verified
	 * to be valid before using this function
	 */
	static int32 GetBestTriangleBaseIndex(const ClothMeshDesc& Mesh, const FVector& Position, float InTolerance = KINDA_SMALL_NUMBER)
	{
		float MinimumDistanceSq = MAX_flt;
		int32 ClosestBaseIndex = INDEX_NONE;

		const TArray<int32> Tris = const_cast<ClothMeshDesc&>(Mesh).FindCandidateTriangles(Position, InTolerance);
		int32 NumTriangles = Tris.Num();
		if (!NumTriangles)
		{
			NumTriangles = Mesh.Indices.Num() / 3;
		}
		for (int32 TriIdx = 0; TriIdx < NumTriangles; ++TriIdx)
		{
			int32 TriBaseIdx = (Tris.Num() ? Tris[TriIdx] : TriIdx) * 3;

			const uint32 IA = Mesh.Indices[TriBaseIdx + 0];
			const uint32 IB = Mesh.Indices[TriBaseIdx + 1];
			const uint32 IC = Mesh.Indices[TriBaseIdx + 2];

			const FVector& A = Mesh.Positions[IA];
			const FVector& B = Mesh.Positions[IB];
			const FVector& C = Mesh.Positions[IC];

			FVector PointOnTri = FMath::ClosestPointOnTriangleToPoint(Position, A, B, C);
			float DistSq = (PointOnTri - Position).SizeSquared();

			if (DistSq < MinimumDistanceSq)
			{
				MinimumDistanceSq = DistSq;
				ClosestBaseIndex = TriBaseIdx;
			}
		}

		return ClosestBaseIndex;
	}

	namespace    // helpers
	{

		float DistanceToTriangle(const FVector& Position, const ClothMeshDesc& Mesh, int32 TriBaseIdx)
		{
			const uint32 IA = Mesh.Indices[TriBaseIdx + 0];
			const uint32 IB = Mesh.Indices[TriBaseIdx + 1];
			const uint32 IC = Mesh.Indices[TriBaseIdx + 2];

			const FVector& A = Mesh.Positions[IA];
			const FVector& B = Mesh.Positions[IB];
			const FVector& C = Mesh.Positions[IC];

			FVector PointOnTri = FMath::ClosestPointOnTriangleToPoint(Position, A, B, C);
			return (PointOnTri - Position).Size();
		}

		using TriangleDistance = TPair<int32, float>;

		/** Similar to GetBestTriangleBaseIndex but returns the N closest triangles. */
		template<uint32 N>
		TStaticArray<TriangleDistance, N> GetNBestTrianglesBaseIndices(const ClothMeshDesc& Mesh, const FVector& Position)
		{
			const TArray<int32> Tris = const_cast<ClothMeshDesc&>(Mesh).FindCandidateTriangles(Position);
			int32 NumTriangles = Tris.Num();

			if (NumTriangles < N)
			{
				// Couldn't find N candidates using FindCandidateTriangles. Grab all triangles in the mesh and see if
				// we have enough
				NumTriangles = Mesh.Indices.Num() / 3;

				if (NumTriangles < N)
				{
					// The mesh doesn't have N triangles. Get as many as we can.
					TStaticArray<TriangleDistance, N> ClosestTriangles;

					int32 i = 0;
					for ( ; i < NumTriangles; ++i)
					{
						int32 TriBaseIdx = 3*i;
						float CurrentDistance = DistanceToTriangle(Position, Mesh, TriBaseIdx);
						ClosestTriangles[i] = TPair<int32, float>{ TriBaseIdx, CurrentDistance };
					}

					// fill the rest with INDEX_NONE 
					for (; i < N; ++i)
					{
						ClosestTriangles[i] = TPair<int32, float>{ INDEX_NONE, 0.0f };
					}

					return ClosestTriangles;
				}
			}

			check(NumTriangles >= N);

			// Closest N triangle, heapified so that the first triangle is the furthest of the N closest triangles
			TStaticArray<TriangleDistance, N> ClosestTriangles;

			// Get the distances to the first N triangles (unsorted)
			int32 i = 0;
			for (; i < N; ++i)
			{
				int32 TriBaseIdx = (Tris.Num() >= N ? Tris[i] : i) * 3;
				float CurrentDistance = DistanceToTriangle(Position, Mesh, TriBaseIdx);
				ClosestTriangles[i] = TPair<int32, float>{ TriBaseIdx, CurrentDistance };
			}

			// Max-heapify the N first triangle distances
			Algo::Heapify(ClosestTriangles, [](const TriangleDistance& A, const TriangleDistance& B)
			{
				return A.Value > B.Value;
			});

			// Now keep going
			for (; i < NumTriangles; ++i)
			{
				int32 TriBaseIdx = (Tris.Num() >= N ? Tris[i] : i) * 3;
				float CurrentDistance = DistanceToTriangle(Position, Mesh, TriBaseIdx);

				if (CurrentDistance < ClosestTriangles[0].Value)
				{
					// Triangle is closer than the "furthest" ClosestTriangles

					// Replace the furthest ClosestTriangles with this triangle...
					ClosestTriangles[0] = TPair<int32, float>{ TriBaseIdx, CurrentDistance };

					// ...and re-heapify the closest triangles
					Algo::Heapify(ClosestTriangles, [](const TriangleDistance& A, const TriangleDistance& B)
					{
						return A.Value > B.Value;
					});
				}
			}

			return ClosestTriangles;
		}

		// Using this formula, for R = Distance / MaxDistance:
		//		Weight = 1 - 3 * R ^ 2 + 3 * R ^ 4 - R ^ 6
		// From the Houdini metaballs docs: https://www.sidefx.com/docs/houdini/nodes/sop/metaball.html#kernels
		// Which was linked to from the cloth capture doc: https://www.sidefx.com/docs/houdini/nodes/sop/clothcapture.html

		float Kernel(float Distance, float MaxDistance)
		{
			float R = FMath::Max(0.0f, FMath::Min(1.0f, Distance / MaxDistance));
			float R2 = R * R;
			float R4 = R2 * R2;
			return 1.0f + 3.0f * (R4 - R2) - R4 * R2;
		}

		template<unsigned int NUM_INFLUENCES>
		bool SkinningDataForVertex(TStaticArray<FMeshToMeshVertData, NUM_INFLUENCES>& SkinningData,
			const ClothMeshDesc& TargetMesh,
			const TArray<FVector3f>* TargetTangents,
			const ClothMeshDesc& SourceMesh,
			int32 VertIdx0,
			float KernelMaxDistance)
		{
			const FVector3f& VertPosition = TargetMesh.Positions[VertIdx0];
			const FVector3f& VertNormal = TargetMesh.Normals[VertIdx0];

			FVector VertTangent;
			if (TargetTangents)
			{
				VertTangent = (*TargetTangents)[VertIdx0];
			}
			else
			{
				FVector3f Tan0, Tan1;
				VertNormal.FindBestAxisVectors(Tan0, Tan1);
				VertTangent = Tan0;
			}

			TStaticArray<TriangleDistance, NUM_INFLUENCES> NearestTriangles =
				GetNBestTrianglesBaseIndices<NUM_INFLUENCES>(SourceMesh, VertPosition);

			float SumWeight = 0.0f;

			for (int j = 0; j < NUM_INFLUENCES; ++j)
			{
				FMeshToMeshVertData& CurrentData = SkinningData[j];

				int ClosestTriangleBaseIdx = NearestTriangles[j].Key;
				if (ClosestTriangleBaseIdx == INDEX_NONE)
				{
					CurrentData.Weight = 0.0f;
					CurrentData.SourceMeshVertIndices[3] = 0xFFFF;
					continue;
				}

				const FVector3f& A = SourceMesh.Positions[SourceMesh.Indices[ClosestTriangleBaseIdx]];
				const FVector3f& B = SourceMesh.Positions[SourceMesh.Indices[ClosestTriangleBaseIdx + 1]];
				const FVector3f& C = SourceMesh.Positions[SourceMesh.Indices[ClosestTriangleBaseIdx + 2]];

				const FVector3f& NA = SourceMesh.Normals[SourceMesh.Indices[ClosestTriangleBaseIdx]];
				const FVector3f& NB = SourceMesh.Normals[SourceMesh.Indices[ClosestTriangleBaseIdx + 1]];
				const FVector3f& NC = SourceMesh.Normals[SourceMesh.Indices[ClosestTriangleBaseIdx + 2]];

				// Before generating the skinning data we need to check for a degenerate triangle.
				// If we find _any_ degenerate triangles we will notify and fail to generate the skinning data
				const FVector TriNormal = FVector::CrossProduct(B - A, C - A);
				if (TriNormal.SizeSquared() < SMALL_NUMBER)
				{
					// Failed, we have 2 identical vertices

					// Log and toast
					FText Error = FText::Format(LOCTEXT("DegenerateTriangleError", "Failed to generate skinning data, found conincident vertices in triangle A={0} B={1} C={2}"), FText::FromString(A.ToString()), FText::FromString(B.ToString()), FText::FromString(C.ToString()));

					UE_LOG(LogClothingMeshUtils, Warning, TEXT("%s"), *Error.ToString());

#if WITH_EDITOR
					FNotificationInfo Info(Error);
					Info.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(Info);
#endif
					return false;
				}

				CurrentData.PositionBaryCoordsAndDist = GetPointBaryAndDist(A, B, C, VertPosition);
				CurrentData.NormalBaryCoordsAndDist = GetPointBaryAndDist(A, B, C, VertPosition + VertNormal);
				CurrentData.TangentBaryCoordsAndDist = GetPointBaryAndDist(A, B, C, VertPosition + VertTangent);
				CurrentData.SourceMeshVertIndices[0] = SourceMesh.Indices[ClosestTriangleBaseIdx];
				CurrentData.SourceMeshVertIndices[1] = SourceMesh.Indices[ClosestTriangleBaseIdx + 1];
				CurrentData.SourceMeshVertIndices[2] = SourceMesh.Indices[ClosestTriangleBaseIdx + 2];
				CurrentData.SourceMeshVertIndices[3] = 0;

				CurrentData.Weight = Kernel(NearestTriangles[j].Value, KernelMaxDistance);
				SumWeight += CurrentData.Weight;
			}

			// Normalize weights

			if (SumWeight == 0.0f)
			{
				// Abort
				for (int j = 0; j < NUM_INFLUENCES; ++j)
				{
					FMeshToMeshVertData& CurrentData = SkinningData[j];
					CurrentData.SourceMeshVertIndices[3] = 0xFFFF;
					CurrentData.Weight = 0.0f;
				}
			}

			for (FMeshToMeshVertData& CurrentData : SkinningData)
			{
				CurrentData.Weight /= SumWeight;
			}

			return true;
		}

	}		// unnamed namespace


	void GenerateMeshToMeshSkinningData(TArray<FMeshToMeshVertData>& OutSkinningData, 
		const ClothMeshDesc& TargetMesh, 
		const TArray<FVector3f>* TargetTangents, 
		const ClothMeshDesc& SourceMesh,
		bool bUseMultipleInfluences,
		float KernelMaxDistance)
	{
		if(!TargetMesh.HasValidMesh())
		{
			UE_LOG(LogClothingMeshUtils, Warning, TEXT("Failed to generate mesh to mesh skinning data. Invalid Target Mesh."));
			return;
		}

		if(!SourceMesh.HasValidMesh())
		{
			UE_LOG(LogClothingMeshUtils, Warning, TEXT("Failed to generate mesh to mesh skinning data. Invalid Source Mesh."));
			return;
		}

		const int32 NumMesh0Verts = TargetMesh.Positions.Num();
		const int32 NumMesh0Normals = TargetMesh.Normals.Num();
		const int32 NumMesh0Tangents = TargetTangents ? TargetTangents->Num() : 0;

		const int32 NumMesh1Verts = SourceMesh.Positions.Num();
		const int32 NumMesh1Normals = SourceMesh.Normals.Num();
		const int32 NumMesh1Indices = SourceMesh.Indices.Num();

		// Check we have properly formed triangles
		check(NumMesh1Indices % 3 == 0);

		const int32 NumMesh1Triangles = NumMesh1Indices / 3;

		// Check mesh data to make sure we have the same number of each element
		if(NumMesh0Verts != NumMesh0Normals || (TargetTangents && NumMesh0Tangents != NumMesh0Verts))
		{
			UE_LOG(LogClothingMeshUtils, Warning, TEXT("Can't generate mesh to mesh skinning data, Mesh0 data is missing verts."));
			return;
		}

		if(NumMesh1Verts != NumMesh1Normals)
		{
			UE_LOG(LogClothingMeshUtils, Warning, TEXT("Can't generate mesh to mesh skinning data, Mesh1 data is missing verts."));
			return;
		}

		if (bUseMultipleInfluences)
		{
			OutSkinningData.Reserve(NumMesh0Verts * NUM_INFLUENCES_PER_VERTEX);

			// For all mesh0 verts
			for (int32 VertIdx0 = 0; VertIdx0 < NumMesh0Verts; ++VertIdx0)
			{
				TStaticArray<FMeshToMeshVertData, NUM_INFLUENCES_PER_VERTEX> SkinningData;
				bool bOK = SkinningDataForVertex(SkinningData,
												 TargetMesh,
												 TargetTangents,
												 SourceMesh,
												 VertIdx0,
												 KernelMaxDistance);

				// If we find _any_ degenerate triangles we will notify and fail to generate the skinning data
				if (!bOK)
				{
					UE_LOG(LogClothingMeshUtils, Warning, TEXT("Error generating mesh-to-mesh skinning data"));
					OutSkinningData.Reset();
					return;
				}

				OutSkinningData.Append(SkinningData.GetData(), NUM_INFLUENCES_PER_VERTEX);
			}

			check(OutSkinningData.Num() == NumMesh0Verts * NUM_INFLUENCES_PER_VERTEX);
		}
		else
		{
			OutSkinningData.Reserve(NumMesh0Verts);

			// Compute the max edge length for each edge coming out of a target vertex and 
			// use that to guide the search radius for the source mesh triangles.
			TArray<float> MaxEdgeLength;
			MaxEdgeLength.Init(0.0f, NumMesh0Verts);

			for (int32 TriangleIdx = 0; TriangleIdx < NumMesh0Verts; ++TriangleIdx)
			{
				const uint32* Triangle = &TargetMesh.Indices[TriangleIdx * 3]; 
				
				for (int32 Vertex0Idx = 0; Vertex0Idx < 3; Vertex0Idx++)
				{
					const int32 Vertex1Idx = (Vertex0Idx + 1) % 3;

					const FVector& P0 = TargetMesh.Positions[Triangle[Vertex0Idx]];
					const FVector& P1 = TargetMesh.Positions[Triangle[Vertex1Idx]];

					const float EdgeLength = FVector::Distance(P0, P1);
					MaxEdgeLength[Triangle[Vertex0Idx]] = FMath::Max(MaxEdgeLength[Triangle[Vertex0Idx]], EdgeLength);
					MaxEdgeLength[Triangle[Vertex1Idx]] = FMath::Max(MaxEdgeLength[Triangle[Vertex1Idx]], EdgeLength);
				}
			}			

			// For all mesh0 verts
			for (int32 VertIdx0 = 0; VertIdx0 < NumMesh0Verts; ++VertIdx0)
			{
				OutSkinningData.AddZeroed();
				FMeshToMeshVertData& SkinningData = OutSkinningData.Last();

				const FVector3f& VertPosition = TargetMesh.Positions[VertIdx0];
				const FVector3f& VertNormal = TargetMesh.Normals[VertIdx0];

				FVector VertTangent;
				if (TargetTangents)
				{
					VertTangent = (*TargetTangents)[VertIdx0];
				}
				else
				{
					FVector3f Tan0, Tan1;
					VertNormal.FindBestAxisVectors(Tan0, Tan1);
					VertTangent = Tan0;
				}

				const int32 ClosestTriangleBaseIdx = GetBestTriangleBaseIndex(SourceMesh, VertPosition, MaxEdgeLength[VertIdx0]);
				check(ClosestTriangleBaseIdx != INDEX_NONE);

				const FVector3f& A = SourceMesh.Positions[SourceMesh.Indices[ClosestTriangleBaseIdx]];
				const FVector3f& B = SourceMesh.Positions[SourceMesh.Indices[ClosestTriangleBaseIdx + 1]];
				const FVector3f& C = SourceMesh.Positions[SourceMesh.Indices[ClosestTriangleBaseIdx + 2]];

				const FVector3f& NA = SourceMesh.Normals[SourceMesh.Indices[ClosestTriangleBaseIdx]];
				const FVector3f& NB = SourceMesh.Normals[SourceMesh.Indices[ClosestTriangleBaseIdx + 1]];
				const FVector3f& NC = SourceMesh.Normals[SourceMesh.Indices[ClosestTriangleBaseIdx + 2]];

				// Before generating the skinning data we need to check for a degenerate triangle.
				// If we find _any_ degenerate triangles we will notify and fail to generate the skinning data
				const FVector TriNormal = FVector::CrossProduct(B - A, C - A);
				if (TriNormal.SizeSquared() < SMALL_NUMBER)
				{
					// Failed, we have 2 identical vertices
					OutSkinningData.Reset();

					// Log and toast
					FText Error = FText::Format(LOCTEXT("DegenerateTriangleError", "Failed to generate skinning data, found conincident vertices in triangle A={0} B={1} C={2}"), FText::FromString(A.ToString()), FText::FromString(B.ToString()), FText::FromString(C.ToString()));

					UE_LOG(LogClothingMeshUtils, Warning, TEXT("%s"), *Error.ToString());

#if WITH_EDITOR
					FNotificationInfo Info(Error);
					Info.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(Info);
#endif
					return;
				}

				SkinningData.PositionBaryCoordsAndDist = GetPointBaryAndDistWithNormals(A, B, C, NA, NB, NC, VertPosition);
				SkinningData.NormalBaryCoordsAndDist = GetPointBaryAndDistWithNormals(A, B, C, NA, NB, NC, VertPosition + VertNormal);
				SkinningData.TangentBaryCoordsAndDist = GetPointBaryAndDistWithNormals(A, B, C, NA, NB, NC, VertPosition + VertTangent);
				SkinningData.SourceMeshVertIndices[0] = SourceMesh.Indices[ClosestTriangleBaseIdx];
				SkinningData.SourceMeshVertIndices[1] = SourceMesh.Indices[ClosestTriangleBaseIdx + 1];
				SkinningData.SourceMeshVertIndices[2] = SourceMesh.Indices[ClosestTriangleBaseIdx + 2];
				SkinningData.SourceMeshVertIndices[3] = 0;
				SkinningData.Weight = 1.0f;
			}

			check(OutSkinningData.Num() == NumMesh0Verts);
		}
	}

	// TODO: Vertex normals are not used at present, a future improved algorithm might however
	FVector4 GetPointBaryAndDist(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& Point)
	{
		FPlane4f TrianglePlane(A, B, C);
		const FVector3f PointOnTriPlane = FVector::PointPlaneProject(Point, TrianglePlane);
		const FVector3f BaryCoords = FMath::ComputeBaryCentric2D(PointOnTriPlane, A, B, C);
		return FVector4(BaryCoords, TrianglePlane.PlaneDot(Point)); // Note: The normal of the plane points away from the Clockwise face (instead of the counter clockwise face) in Left Handed Coordinates (This is why we need to invert the normals later on when before sending it to the shader)
	}


	
	double TripleProduct(const FVector3f& A, const FVector3f& B, const FVector3f& C)
	{
		return FVector3f::DotProduct(A, FVector3f::CrossProduct(B,C));
	}

	// Solve the equation x^2 + Ax + B = 0 for real roots. 
	// Requires an array of size 2 for the results. The return value is the number of results,
	// either 0 or 2.
	int32 QuadraticRoots(double Result[], double A, double B)
	{
		double D = 0.25 * A * A - B;
		if (D >= 0.0)
		{
			D = FMath::Sqrt(D);
			Result[0] = -0.5 * A + D; 
			Result[1] = -0.5 * A - D;
			return 2; 
		}
		return 0;
	}
	
	// Solve the equation x^3 + Ax^2 + Bx + C = 0 for real roots. Requires an array of size 3 
	// for the results. The return value is the number of results, ranging from 1 to 3.
	// Using Viete's trig formula. See: https://en.wikipedia.org/wiki/Cubic_equation
	int32 CubicRoots(double Result[], double A, double B, double C)
	{
		double A2 = A * A;
		double P = (A2 - 3.0 * B) / 9.0;
		double Q = (A * (2.0 * A2 - 9.0 * B) + 27.0 * C) / 54.0;
		double P3 = P * P * P;
		double Q2 = Q * Q;
		if (Q2 <= (P3 + SMALL_NUMBER))
		{
			// We have three real roots.
			double T = Q / FMath::Sqrt(P3);
			// FMath::Acos automatically clamps T to [-1,1]
			T = FMath::Acos(T);
			A /= 3.0;
			P = -2.0 * FMath::Sqrt(P);
			Result[0] = P * FMath::Cos(T / 3.0) - A;
			Result[1] = P * FMath::Cos((T + 2.0 * PI) / 3.0) - A;
			Result[2] = P * FMath::Cos((T - 2.0 * PI) / 3.0) - A;
			return 3;
		}
		else
		{
			// One or two real roots.
			double R_1 = FMath::Pow(FMath::Abs(Q) + FMath::Sqrt(Q2 - P3), 1.0 / 3.0);
			if (Q > 0.0)
			{
				R_1 = -R_1;
			}
			double R_2 = 0.0;
			if (!FMath::IsNearlyZero(A))
			{
				R_2 = P / R_1;
			}
			A /= 3.0;
			Result[0] = (R_1 + R_2) - A;
			
			if (!FMath::IsNearlyZero(UE_HALF_SQRT_3 * (R_1 - R_2)))
			{
				return 1;
			}
			
			// Yoda: No. There is another...
			Result[1] = -0.5 * (R_1 + R_2) - A;
			return 2;
		}
	}

	static int32 CoplanarityParam(
	const FVector3f& A, const FVector3f& B, const FVector3f& C,
	   const FVector3f& OffsetA, const FVector3f& OffsetB, const FVector3f& OffsetC,
	   const FVector3f& Point, double Out[3])
	{
		FVector3f PA = A - Point;
		FVector3f PB = B - Point;
		FVector3f PC = C - Point;

		double Coeffs[4] = {
			TripleProduct(OffsetA, OffsetB, OffsetC),
			TripleProduct(PA, OffsetB, OffsetC) + TripleProduct(OffsetA, PB, OffsetC) + TripleProduct(OffsetA, OffsetB, PC),
			TripleProduct(PA, PB, OffsetC) + TripleProduct(PA, OffsetB, PC) + TripleProduct(OffsetA, PB, PC),
			TripleProduct(PA, PB, PC)
			};

		// Solve cubic A*w^3 + B*w^2 + C*w + D
		if (FMath::IsNearlyZero(Coeffs[0], double(KINDA_SMALL_NUMBER)))
		{
			// In this case, the tetrahedron formed above is probably already at zero volume,
			// which means the point is coplanar to the triangle without normal offsets.
			// Just compute the signed distance.
			const FPlane4f TrianglePlane(A, B, C);
			Out[0] = -TrianglePlane.PlaneDot(Point);
			return 1;
		}
		else
		{
			for (int32 I = 1; I < 4; I++)
			{
				Coeffs[I] /= Coeffs[0];
			}
			
			return CubicRoots(Out, Coeffs[1], Coeffs[2], Coeffs[3]);
		}
	}

	FVector4 GetPointBaryAndDistWithNormals(
		const FVector3f& A, const FVector3f& B, const FVector3f& C,
		const FVector3f& NA, const FVector3f& NB, const FVector3f& NC,
		const FVector3f& Point)
	{
		// Adapted from cloth CCD paper [Bridson et al. 2002]
		// First find W such that Point lies in the plane defined by {A+wNA, B+wNB, C+wNC}
		// Pass in inverted normals, since they get inverted at runtime (Left handed system).
		double W[3];
		const int32 Count = CoplanarityParam(A, B, C, NA, NB, NC, Point, W);

		if (Count == 0)
		{
			return GetPointBaryAndDist(A, B, C, Point);
		}

		FVector4 BaryAndDist;
		double MinDistanceSq = std::numeric_limits<double>::max();

		// If the solution gives us barycentric coordinates that lie purely within the triangle,
		// then choose that. Otherwise try to minimize the distance of the projected point to
		// be as close to the triangle as possible.
		for (int32 Index = 0; Index < Count; Index++)
		{
			// Then find the barycentric coordinates of Point wrt {A+wNA, B+wNB, C+wNC}
			FVector AW = A + W[Index] * NA;
			FVector BW = B + W[Index] * NB;
			FVector CW = C + W[Index] * NC;

			FPlane4f TrianglePlane(AW, BW, CW);

			const FVector3f PointOnTriPlane = FVector::PointPlaneProject(Point, TrianglePlane);
			const FVector3f BaryCoords = FMath::ComputeBaryCentric2D(PointOnTriPlane, AW, BW, CW);

			if (BaryCoords.X >= 0.0f && BaryCoords.X <= 1.0f &&
				BaryCoords.Y >= 0.0f && BaryCoords.Y <= 1.0f &&
				BaryCoords.Z >= 0.0f && BaryCoords.Z <= 1.0f)
			{
				BaryAndDist = FVector4(BaryCoords, -W[Index]);
				break;
			}
			
			const float DistSq = FMath::Square(BaryCoords.X - 0.5) +
								 FMath::Square(BaryCoords.Y - 0.5) +
								 FMath::Square(BaryCoords.Z - 0.5);
			
			if (DistSq < MinDistanceSq)
			{
				BaryAndDist = FVector4(BaryCoords, -W[Index]);
				MinDistanceSq = DistSq;
			}
		}

		const FVector3f ReprojectedPoint =
			BaryAndDist.X * (A - NA * BaryAndDist.W) +
			BaryAndDist.Y * (B - NB * BaryAndDist.W) +
			BaryAndDist.Z * (C - NC * BaryAndDist.W);

		const float Distance = FVector::Distance(Point, ReprojectedPoint);

		// Check if the reprojected point is far from the original. If it is, fall back on
		// the old method of computing the bary values.
		// FIXME: Should we test other cage triangles instead? It's possible that
		// GetBestTriangleBaseIndex is not actually picking the /best/ one.
		const bool bRequireFallback = !FMath::IsNearlyZero(Distance, KINDA_SMALL_NUMBER); 

		if (bRequireFallback)
		{
			return GetPointBaryAndDist(A, B, C, Point);
		}
		
		return BaryAndDist;
	}


	void GenerateEmbeddedPositions(const ClothMeshDesc& SourceMesh, TArrayView<const FVector3f> Positions, TArray<FVector4>& OutEmbeddedPositions, TArray<int32>& OutSourceIndices)
	{
		if(!SourceMesh.HasValidMesh())
		{
			// No valid source mesh
			return;
		}

		const int32 NumPositions = Positions.Num();

		OutEmbeddedPositions.Reset();
		OutEmbeddedPositions.AddUninitialized(NumPositions);

		OutSourceIndices.Reset(NumPositions * 3);

		for(int32 PositionIndex = 0 ; PositionIndex < NumPositions ; ++PositionIndex)
		{
			const FVector3f& Position = Positions[PositionIndex];

			int32 TriBaseIndex = GetBestTriangleBaseIndex(SourceMesh, Position);

			const int32 IA = SourceMesh.Indices[TriBaseIndex];
			const int32 IB = SourceMesh.Indices[TriBaseIndex + 1];
			const int32 IC = SourceMesh.Indices[TriBaseIndex + 2];

			const FVector3f& A = SourceMesh.Positions[IA];
			const FVector3f& B = SourceMesh.Positions[IB];
			const FVector3f& C = SourceMesh.Positions[IC];

			const FVector3f& NA = SourceMesh.Normals[IA];
			const FVector3f& NB = SourceMesh.Normals[IB];
			const FVector3f& NC = SourceMesh.Normals[IC];

			OutEmbeddedPositions[PositionIndex] = GetPointBaryAndDistWithNormals(A, B, C, NA, NB, NC, Position);
			OutSourceIndices.Add(IA);
			OutSourceIndices.Add(IB);
			OutSourceIndices.Add(IC);
		}
	}


	void ComputeVertexContributions(
		TArray<FMeshToMeshVertData>& InOutSkinningData,
		const FPointWeightMap* const InMaxDistances,
		const bool bInSmoothTransition
		)
	{
		if (InMaxDistances && InMaxDistances->Num())
		{
			for (FMeshToMeshVertData& VertData : InOutSkinningData)
			{
				const bool IsStatic0 = InMaxDistances->IsBelowThreshold(VertData.SourceMeshVertIndices[0]);
				const bool IsStatic1 = InMaxDistances->IsBelowThreshold(VertData.SourceMeshVertIndices[1]);
				const bool IsStatic2 = InMaxDistances->IsBelowThreshold(VertData.SourceMeshVertIndices[2]);

				// None of the cloth vertices will move due to max distance constraints.
				if (IsStatic0 && IsStatic1 && IsStatic2)
				{
					VertData.SourceMeshVertIndices[3] = 0xFFFF;
				}
				// If all of the vertices are dynamic _or_ if we disallow smooth transition,
				// ensure there's no blending between cloth and skinned mesh and that the cloth
				// mesh dominates.
				else if ((!IsStatic0 && !IsStatic1 && !IsStatic2) || !bInSmoothTransition)
				{
					VertData.SourceMeshVertIndices[3] = 0;
				}
				else
				{
					// Compute how much the vertex actually contributes. A value of 0xFFFF
					// means that it stays static relative to the skinned mesh, a value of 0x0000
					// means that only the cloth simulation contributes. 
					float StaticAlpha = 
						IsStatic0 * VertData.PositionBaryCoordsAndDist.X +
						IsStatic1 * VertData.PositionBaryCoordsAndDist.Y +
						IsStatic2 * VertData.PositionBaryCoordsAndDist.Z;
					StaticAlpha = FMath::Clamp(StaticAlpha, 0.0f, 1.0f);
					
					VertData.SourceMeshVertIndices[3] = static_cast<uint16>(StaticAlpha * 0xFFFF);
				}	
			}
		}
		else
		{
			// Can't determine contribution from the max distance map, so the entire mesh overrides.
			for (FMeshToMeshVertData& VertData : InOutSkinningData)
			{
				VertData.SourceMeshVertIndices[3] = 0;
			}
		}
		
	}


	void FVertexParameterMapper::Map(TArrayView<const float> Source, TArray<float>& Dest)
	{
		Map(Source, Dest, [](FVector3f Bary, float A, float B, float C)
		{
			return Bary.X * A + Bary.Y * B + Bary.Z * C;
		});
	}


	TArray<int32> ClothMeshDesc::FindCandidateTriangles(const FVector& InPoint, float InTolerance)
	{
		ensure(HasValidMesh());
		static const int32 MinNumTrianglesForBVHCreation = 100;
		const int32 NumTris = Indices.Num() / 3;
		if (NumTris > MinNumTrianglesForBVHCreation)
		{
			// This is not thread safe
			if (!bHasValidBVH)
			{
				TArray<FClothBvEntry> BVEntries;
				BVEntries.Reset(NumTris);

				for (int32 Tri = 0; Tri < NumTris; ++Tri)
				{
					BVEntries.Add({ this, Tri });
				}
				BVH.Reinitialize(BVEntries);
				bHasValidBVH = true;
			}
			Chaos::FAABB3 TmpAABB(InPoint, InPoint);
			TmpAABB.Thicken(InTolerance);  // Most points might be very close to the triangle, but not directly on it
			TArray<int32> Triangles = BVH.FindAllIntersections(TmpAABB);

			// Refine the search to include all nearby bounded volumes (the point could well be outside the closest triangle's bounded volume)
			if (Triangles.Num())
			{
				float ClosestDistance = TNumericLimits<float>::Max();
				for (const int32 Triangle : Triangles)
				{
					ClosestDistance = FMath::Min(ClosestDistance, DistanceToTriangle(InPoint, *this, Triangle * 3));
				}

				TmpAABB.Thicken(ClosestDistance);
				return BVH.FindAllIntersections(TmpAABB);
			}
		}
		return TArray<int32>();
	}
}

#undef LOCTEXT_NAMESPACE
