// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/CollectionClothLodFacade.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/NonManifoldMappingSupport.h"
#include "ToDynamicMesh.h"
#include "Util/IndexUtil.h"

// Utility functions to unwrap a 3d sim mesh into a tailored cloth
namespace UE::Chaos::ClothAsset::Private
{
	struct FSimpleSrcMeshInterface
	{
 		typedef int32     VertIDType;
 		typedef int32     TriIDType;
 
		FSimpleSrcMeshInterface(const TArray<FVector3f>& InPositions, const TArray<uint32>& InIndices)
			:Positions(InPositions), Indices(InIndices)
		{
			VertIDs.SetNumUninitialized(Positions.Num());
			for (int32 VtxIndex = 0; VtxIndex < Positions.Num(); ++VtxIndex)
			{
				VertIDs[VtxIndex] = VtxIndex;
			}
			
			check(Indices.Num() % 3 == 0);
			const int32 NumFaces = Indices.Num() / 3;
			TriIDs.SetNumUninitialized(NumFaces);
			for (int32 TriIndex = 0; TriIndex < NumFaces; ++TriIndex)
			{
				TriIDs[TriIndex] = 3*TriIndex;
			}
		}

		// accounting.
		int32 NumTris() const { return TriIDs.Num(); }
		int32 NumVerts() const { return VertIDs.Num(); }
 
		// --"Vertex Buffer" info
		const TArray<VertIDType>& GetVertIDs() const { return VertIDs; }
		const FVector GetPosition(const VertIDType VtxID) const { return FVector(Positions[VtxID]); }
 
		// --"Index Buffer" info
		const TArray<TriIDType>& GetTriIDs() const { return TriIDs; }
		// return false if this TriID is not contained in mesh.
		bool GetTri(const TriIDType TriID, VertIDType& VID0, VertIDType& VID1, VertIDType& VID2) const
		{
			VID0 = Indices[TriID + 0];
			VID1 = Indices[TriID + 1];
			VID2 = Indices[TriID + 2];

			return true;
		}

	private:
		const TArray<FVector3f>& Positions;
		const TArray<uint32>& Indices;

		TArray<TriIDType> TriIDs; // TriID = first index in flat Indices array
		TArray<VertIDType> VertIDs;
	};

	// Triangle islands to become patterns, although in this case all the seams are internal (same pattern)
	struct FIsland
	{
		TArray<int32> Indices;  // 3x number of triangles
		TArray<FVector2f> Positions;
		TArray<FVector3f> RestPositions;  // Same size as Positions
		TArray<int32> PositionToSourceIndex; // Same size as Positions. Index in the original welded position array
	};

	enum class EIntersectCirclesResult
	{
		SingleIntersect,
		DoubleIntersect,
		Coincident,
		Separate,
		Contained
	};

	static EIntersectCirclesResult IntersectCircles(const FVector2f& C0, float R0, const FVector2f& C1, float R1, FVector2f& OutI0, FVector2f& OutI1)
	{
		const FVector2f C0C1 = C0 - C1;
		const float D = C0C1.Length();
		if (D < SMALL_NUMBER)
		{
			return EIntersectCirclesResult::Coincident;
		}
		else if (D > R0 + R1)
		{
			return EIntersectCirclesResult::Separate;
		}
		else if (D < FMath::Abs(R0 - R1))
		{
			return EIntersectCirclesResult::Contained;
		}
		const float SquareR0 = R0 * R0;
		const float SquareR1 = R1 * R1;
		const float SquareD = D * D;
		const float A = (SquareD - SquareR1 + SquareR0) / (2.f * D);

		OutI0 = OutI1 = C0 + A * (C1 - C0) / D;

		if (FMath::Abs(A - R0) < SMALL_NUMBER)
		{
			return EIntersectCirclesResult::SingleIntersect;
		}

		const float SquareA = A * A;
		const float H = FMath::Sqrt(SquareR0 - SquareA);

		const FVector2f N(C0C1.Y, -C0C1.X);

		OutI0 += N * H / D;
		OutI1 -= N * H / D;

		return EIntersectCirclesResult::DoubleIntersect;
	}
;
	static FIntVector2 MakeSortedIntVector2(int32 Index0, int32 Index1)
	{
		return Index0 < Index1 ? FIntVector2(Index0, Index1) : FIntVector2(Index1, Index0);
	}

	template<bool bWeldNearlyCoincidentVertices>
	static void UnwrapDynamicMesh(const UE::Geometry::FDynamicMesh3& DynamicMesh, TArray<FIsland>& OutIslands)
	{
		using namespace UE::Geometry;

		OutIslands.Reset();
		constexpr float SquaredWeldingDistance = FMath::Square(0.01f);  // 0.1 mm

		// Build pattern islands. 
		const int32 NumTriangles = DynamicMesh.TriangleCount();
		TSet<int32> VisitedTriangles;
		VisitedTriangles.Reserve(NumTriangles);

		for (int32 SeedTriangle : DynamicMesh.TriangleIndicesItr())
		{
			if (VisitedTriangles.Contains(SeedTriangle))
			{
				continue;
			}
			const FIndex3i TriangleIndices = DynamicMesh.GetTriangle(SeedTriangle);

			const int32 SeedIndex0 = TriangleIndices[0];
			const int32 SeedIndex1 = TriangleIndices[1];

			const FVector3f Position0(DynamicMesh.GetVertex(SeedIndex0));
			const FVector3f Position1(DynamicMesh.GetVertex(SeedIndex1));
			const float Position01DistSq = FVector3f::DistSquared(Position0, Position1);

			if (Position01DistSq <= SquaredWeldingDistance)
			{
				continue;  // A degenerated triangle edge is not a good start
			}

			// Setup first visitor from seed, and add the first two points
			FIsland& Island = OutIslands.AddDefaulted_GetRef();

			Island.RestPositions.Add(Position0);
			Island.RestPositions.Add(Position1);
			Island.PositionToSourceIndex.Add(SeedIndex0);
			Island.PositionToSourceIndex.Add(SeedIndex1);

			const int32 SeedIndex2D0 = Island.Positions.Add(FVector2f::ZeroVector);
			const int32 SeedIndex2D1 = Island.Positions.Add(FVector2f(FMath::Sqrt(Position01DistSq), 0.f));

			struct FVisitor
			{
				int32 Triangle;
				FIndex2i OldEdge;
				FIndex2i NewEdge;
				int32 CrossEdgePoint;  // Keep the opposite point to orientate degenerate cases
			} Visitor =
			{
				SeedTriangle,
				FIndex2i(SeedIndex0, SeedIndex1),
				FIndex2i(SeedIndex2D0, SeedIndex2D1),
				INDEX_NONE
			};

			VisitedTriangles.Add(SeedTriangle);

			TQueue<FVisitor> Visitors;
			do
			{
				const int32 Triangle = Visitor.Triangle;
				const int32 CrossEdgePoint = Visitor.CrossEdgePoint;
				const int32 OldIndex0 = Visitor.OldEdge.A;
				const int32 OldIndex1 = Visitor.OldEdge.B;
				const int32 NewIndex0 = Visitor.NewEdge.A;
				const int32 NewIndex1 = Visitor.NewEdge.B;

				// Find opposite index from this triangle edge

				const int32 OldIndex2 = IndexUtil::FindTriOtherVtxUnsafe(OldIndex0, OldIndex1, DynamicMesh.GetTriangle(Triangle));

				// Find the 2D intersection of the two connecting adjacent edges using the 3D reference length
				const FVector3f P0(DynamicMesh.GetVertexRef(OldIndex0));
				const FVector3f P1(DynamicMesh.GetVertexRef(OldIndex1));
				const FVector3f P2(DynamicMesh.GetVertexRef(OldIndex2));

				const float R0 = FVector3f::Dist(P0, P2);
				const float R1 = FVector3f::Dist(P1, P2);
				const FVector2f& C0 = Island.Positions[NewIndex0];
				const FVector2f& C1 = Island.Positions[NewIndex1];

				FVector2f I0, I1;
				const EIntersectCirclesResult IntersectCirclesResult = IntersectCircles(C0, R0, C1, R1, I0, I1);

				FVector2f C2;
				switch (IntersectCirclesResult)
				{
				case EIntersectCirclesResult::SingleIntersect:
					C2 = I0;  // Degenerated C2 is on (C0C1)
					break;
				case EIntersectCirclesResult::DoubleIntersect:
					C2 = (FVector2f::CrossProduct(C0 - C1, C0 - I0) > 0) ? I0 : I1;  // Keep correct winding order
					break;
				case EIntersectCirclesResult::Coincident:
					check(CrossEdgePoint != INDEX_NONE); // We can't start on a degenerated triangle
					C2 = C0 - (Island.Positions[CrossEdgePoint] - C0).GetSafeNormal() * R0;  // Degenerated C0 == C1, choose C2 on the opposite of the visitor opposite point
					break;
				case EIntersectCirclesResult::Separate: [[fallthrough]];
				case EIntersectCirclesResult::Contained:
					C2 = C0 - (C1 - C0).GetSafeNormal() * R0;  // Degenerated + some tolerance, C2 is on (C0C1)
					break;
				}

				// Add the new position found for the opposite point
				int32 NewIndex2 = INDEX_NONE;
				for (int32 UsedIndex = 0; UsedIndex < Island.Positions.Num(); ++UsedIndex)
				{
					if (bWeldNearlyCoincidentVertices)
					{
						if (FVector2f::DistSquared(Island.Positions[UsedIndex], C2) <= SquaredWeldingDistance &&
							FVector3f::DistSquared(Island.RestPositions[UsedIndex], P2) <= SquaredWeldingDistance)
						{
							NewIndex2 = UsedIndex;  // Both Rest and 2D positions match, reuse this index
							break;
						}
					}
					else
					{
						if(Island.PositionToSourceIndex[UsedIndex] == OldIndex2 &&
							FVector2f::DistSquared(Island.Positions[UsedIndex], C2) <= SquaredWeldingDistance)
						{
							NewIndex2 = UsedIndex;  // Both Rest and 2D positions match, reuse this index
							break;
						}
					}
				}

				if (NewIndex2 == INDEX_NONE)
				{
					NewIndex2 = Island.Positions.Add(C2);
					Island.RestPositions.Add(P2);
					Island.PositionToSourceIndex.Add(OldIndex2);
				}

				// Add triangle to list of indices, unless it is degenerated to a segment
				if (NewIndex0 != NewIndex1 && NewIndex1 != NewIndex2 && NewIndex2 != NewIndex0)
				{
					Island.Indices.Add(NewIndex0);
					Island.Indices.Add(NewIndex1);
					Island.Indices.Add(NewIndex2);
				}

				// Add neighbor triangles to the queue
				const FIndex2i OldEdgeList[3] =
				{
					FIndex2i(OldIndex1, OldIndex0),  // Reversed as to keep the correct winding order
					FIndex2i(OldIndex2, OldIndex1),
					FIndex2i(OldIndex0, OldIndex2)
				};
				const FIndex3i NewEdgeList[3] =
				{
					FIndex3i(NewIndex1, NewIndex0, NewIndex2),  // Adds opposite point index
					FIndex3i(NewIndex2, NewIndex1, NewIndex0),
					FIndex3i(NewIndex0, NewIndex2, NewIndex1)
				};
				for (int32 Edge = 0; Edge < 3; ++Edge)
				{
					const int32 EdgeIndex0 = OldEdgeList[Edge].A;
					const int32 EdgeIndex1 = OldEdgeList[Edge].B;

					const FIndex2i EdgeT = DynamicMesh.GetEdgeT(DynamicMesh.FindEdgeFromTri(EdgeIndex0, EdgeIndex1, Triangle));
					const int32 NeighborTriangle = EdgeT.OtherElement(Triangle);
					if (NeighborTriangle != IndexConstants::InvalidID)
					{
						if (!VisitedTriangles.Contains(NeighborTriangle))
						{
							// Mark neighboring triangle as visited
							VisitedTriangles.Add(NeighborTriangle);

							// Enqueue next triangle
							Visitors.Enqueue(FVisitor
								{
									NeighborTriangle,
									OldEdgeList[Edge],
									FIndex2i(NewEdgeList[Edge].A, NewEdgeList[Edge].B),
									NewEdgeList[Edge].C,  // Pass the cross edge 2D opposite point to help define orientation of any degenerated triangles
								});
						}

					}
				}
			} while (Visitors.Dequeue(Visitor));
		}
	}

	static void BuildIslandsFromDynamicMeshUVs(const UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArray<FIsland>& OutIslands)
	{
		using namespace UE::Geometry;

		const FDynamicMesh3* DynamicMesh = UVOverlay.GetParentMesh();
		check(DynamicMesh);

		OutIslands.Reset();

		// Build pattern islands. 
		const int32 NumTriangles = DynamicMesh->TriangleCount();
		TSet<int32> VisitedTriangles;
		VisitedTriangles.Reserve(NumTriangles);

		// This is reused for each island, but only allocate once.
		TArray<int32> SourceElementIndexToNewIndex;

		for (int32 SeedTriangle : DynamicMesh->TriangleIndicesItr())
		{
			if (VisitedTriangles.Contains(SeedTriangle))
			{
				continue;
			}

			// Setup first visitor from seed
			FIsland& Island = OutIslands.AddDefaulted_GetRef();

			SourceElementIndexToNewIndex.Init(INDEX_NONE, UVOverlay.MaxElementID());

			struct FVisitor
			{
				int32 Triangle;
			} Visitor =
			{
				SeedTriangle
			};

			VisitedTriangles.Add(SeedTriangle);

			TQueue<FVisitor> Visitors;
			do
			{
				const int32 Triangle = Visitor.Triangle;
				const FIndex3i TriangleIndices = DynamicMesh->GetTriangle(Triangle);
				const FIndex3i TriangleUVElements = UVOverlay.GetTriangle(Triangle);

				auto GetOrAddNewIndex = [&UVOverlay, &Island, &SourceElementIndexToNewIndex, &DynamicMesh](int32 ElementId, int32 VertexId)
				{
					int32& NewIndex = SourceElementIndexToNewIndex[ElementId];
					if (NewIndex == INDEX_NONE)
					{
						NewIndex = Island.RestPositions.Add(FVector3f(DynamicMesh->GetVertexRef(VertexId)));
						Island.Positions.Add(UVOverlay.GetElement(ElementId));
						Island.PositionToSourceIndex.Add(VertexId);
					}
					return NewIndex;
				};

				const int32 NewIndex0 = GetOrAddNewIndex(TriangleUVElements[0], TriangleIndices[0]);
				const int32 NewIndex1 = GetOrAddNewIndex(TriangleUVElements[1], TriangleIndices[1]);
				const int32 NewIndex2 = GetOrAddNewIndex(TriangleUVElements[2], TriangleIndices[2]);
				Island.Indices.Add(NewIndex0);
				Island.Indices.Add(NewIndex1);
				Island.Indices.Add(NewIndex2);

				TArray<int32> NeighborTriangles;
				for (int32 LocalVertexId = 0; LocalVertexId < 3; ++LocalVertexId)
				{
					NeighborTriangles.Reset();
					UVOverlay.GetElementTriangles(TriangleUVElements[LocalVertexId], NeighborTriangles);
					for (const int32 NeighborTriangle : NeighborTriangles)
					{
						if (!VisitedTriangles.Contains(NeighborTriangle))
						{
							// Mark neighboring triangle as visited
							VisitedTriangles.Add(NeighborTriangle);

							// Enqueue next triangle
							Visitors.Enqueue(FVisitor({ NeighborTriangle }));
						}
					}
				}
			} while (Visitors.Dequeue(Visitor));
		}
	}

	struct FSeam
	{
		TSet<FIntVector2> Stitches;
		FIntVector2 Patterns;
	};

	// Stitch together any vertices that were split, either via DynamicMesh NonManifoldMapping or UV Unwrap
	static void BuildSeams(const TArray<FIsland>& Islands, const UE::Geometry::FDynamicMesh3& DynamicMesh, TArray<FSeam>& OutSeams)
	{
		OutSeams.Reset();

		const UE::Geometry::FNonManifoldMappingSupport NonManifoldMapping(DynamicMesh);

		TArray<TMap<int32, TArray<int32>>> IslandSourceIndexToPositions;
		IslandSourceIndexToPositions.SetNum(Islands.Num());

		for (int32 IslandIndex = 0; IslandIndex < Islands.Num(); ++IslandIndex)
		{
			const FIsland& Island = Islands[IslandIndex];
			TMap<int32, TArray<int32>>& SourceIndexToPositions = IslandSourceIndexToPositions[IslandIndex];

			// Build reverse lookup to PositionToSourceIndex
			SourceIndexToPositions.Reserve(Island.PositionToSourceIndex.Num());
			for (int32 PositionIndex = 0; PositionIndex < Island.PositionToSourceIndex.Num(); ++PositionIndex)
			{
				const int32 SourceIndex = NonManifoldMapping.GetOriginalNonManifoldVertexID(Island.PositionToSourceIndex[PositionIndex]);
				SourceIndexToPositions.FindOrAdd(SourceIndex).Add(PositionIndex);
			}

			// Find all internal seams
			FSeam InternalSeam;
			InternalSeam.Patterns = FIntVector2(IslandIndex);
			for (const TPair<int32, TArray<int32>>& Source : SourceIndexToPositions)
			{
				for (int32 FirstSourceArrayIdx = 0; FirstSourceArrayIdx < Source.Value.Num() - 1; ++FirstSourceArrayIdx)
				{
					for (int32 SecondSourceArrayIdx = FirstSourceArrayIdx + 1; SecondSourceArrayIdx < Source.Value.Num(); ++SecondSourceArrayIdx)
					{
						InternalSeam.Stitches.Emplace(MakeSortedIntVector2(Source.Value[FirstSourceArrayIdx], Source.Value[SecondSourceArrayIdx]));
					}
				}
			}
			if (InternalSeam.Stitches.Num())
			{
				OutSeams.Emplace(MoveTemp(InternalSeam));
			}


			for (int32 OtherIslandIndex = 0; OtherIslandIndex < IslandIndex; ++OtherIslandIndex)
			{
				// Find all seams between the two islands
				const TMap<int32, TArray<int32>>& OtherSourceIndexToPositions = IslandSourceIndexToPositions[OtherIslandIndex];

				FSeam Seam;
				Seam.Patterns = FIntVector2(OtherIslandIndex, IslandIndex);
				for (const TPair<int32, TArray<int32>>& FirstSource : SourceIndexToPositions)
				{
					if (const TArray<int32>* OtherSource = OtherSourceIndexToPositions.Find(FirstSource.Key))
					{
						for (const int32 FirstSourceVert : FirstSource.Value)
						{
							for (const int32 OtherSourceVert : *OtherSource)
							{
								Seam.Stitches.Emplace(FIntVector2(OtherSourceVert, FirstSourceVert));
							}
						}
					}
				}
				if (Seam.Stitches.Num())
				{
					OutSeams.Emplace(MoveTemp(Seam));
				}
			}
		}
	}
}  // End namespace UE::ClothAsset::Private

namespace UE::Chaos::ClothAsset
{
	int32 FCollectionClothLodConstFacade::GetNumMaterials() const
	{
		return ClothCollection->GetNumElements(ClothCollection->GetMaterialStart(), ClothCollection->GetMaterialEnd(), LodIndex);
	}

	int32 FCollectionClothLodConstFacade::GetNumTetherBatches() const
	{
		return ClothCollection->GetNumElements(ClothCollection->GetTetherBatchStart(), ClothCollection->GetTetherBatchEnd(), LodIndex);
	}

	int32 FCollectionClothLodConstFacade::GetNumSeams() const
	{
		return ClothCollection->GetNumElements(ClothCollection->GetSeamStart(), ClothCollection->GetSeamEnd(), LodIndex);
	}

	int32 FCollectionClothLodConstFacade::GetNumPatterns() const
	{
		return ClothCollection->GetNumElements(ClothCollection->GetPatternStart(), ClothCollection->GetPatternEnd(), LodIndex);
	}

	TConstArrayView<FString> FCollectionClothLodConstFacade::GetRenderMaterialPathName() const
	{
		return ClothCollection->GetElements(ClothCollection->GetRenderMaterialPathName(), ClothCollection->GetMaterialStart(), ClothCollection->GetMaterialEnd(), LodIndex);
	}

	TConstArrayView<FIntVector2> FCollectionClothLodConstFacade::GetSeamPatterns() const
	{
		return ClothCollection->GetElements(ClothCollection->GetSeamPatterns(), ClothCollection->GetSeamStart(), ClothCollection->GetSeamEnd(), LodIndex);
	}

	TConstArrayView<TArray<FIntVector2>> FCollectionClothLodConstFacade::GetSeamStitches() const
	{
		return ClothCollection->GetElements(ClothCollection->GetSeamStitches(), ClothCollection->GetSeamStart(), ClothCollection->GetSeamEnd(), LodIndex);
	}

	FCollectionClothPatternConstFacade FCollectionClothLodConstFacade::GetPattern(int32 PatternIndex) const
	{
		return FCollectionClothPatternConstFacade(ClothCollection, LodIndex, PatternIndex);
	}

	const FString& FCollectionClothLodConstFacade::GetPhysicsAssetPathName() const
	{
		static const FString EmptyString;
		return ClothCollection->GetPhysicsAssetPathName() ? (*ClothCollection->GetPhysicsAssetPathName())[LodIndex] : EmptyString;
	}

	const FString& FCollectionClothLodConstFacade::GetSkeletonAssetPathName() const
	{
		static const FString EmptyString;
		return ClothCollection->GetSkeletonAssetPathName() ? (*ClothCollection->GetSkeletonAssetPathName())[LodIndex] : EmptyString;
	}

	int32 FCollectionClothLodConstFacade::GetNumSimVertices() const
	{
		return ClothCollection->GetNumSubElements(
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<FVector2f> FCollectionClothLodConstFacade::GetSimPosition() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetSimPosition(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<FVector3f> FCollectionClothLodConstFacade::GetSimRestPosition() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetSimRestPosition(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<FVector3f> FCollectionClothLodConstFacade::GetSimRestNormal() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetSimRestNormal(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<int32> FCollectionClothLodConstFacade::GetSimNumBoneInfluences() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetSimNumBoneInfluences(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<TArray<int32>> FCollectionClothLodConstFacade::GetSimBoneIndices() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetSimBoneIndices(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<TArray<float>> FCollectionClothLodConstFacade::GetSimBoneWeights() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetSimBoneWeights(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			LodIndex);
	}

	int32 FCollectionClothLodConstFacade::GetNumSimFaces() const
	{
		return ClothCollection->GetNumSubElements(
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimFacesStart(),
			ClothCollection->GetSimFacesEnd(),
			LodIndex);
	}

	TConstArrayView<FIntVector3> FCollectionClothLodConstFacade::GetSimIndices() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetSimIndices(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimFacesStart(),
			ClothCollection->GetSimFacesEnd(),
			LodIndex);
	}

	int32 FCollectionClothLodConstFacade::GetNumRenderVertices() const
	{
		return ClothCollection->GetNumSubElements(
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<FVector3f> FCollectionClothLodConstFacade::GetRenderPosition() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderPosition(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<FVector3f> FCollectionClothLodConstFacade::GetRenderNormal() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderNormal(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<FVector3f> FCollectionClothLodConstFacade::GetRenderTangentU() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderTangentU(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<FVector3f> FCollectionClothLodConstFacade::GetRenderTangentV() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderTangentV(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<TArray<FVector2f>> FCollectionClothLodConstFacade::GetRenderUVs() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderUVs(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<FLinearColor> FCollectionClothLodConstFacade::GetRenderColor() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderColor(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<int32> FCollectionClothLodConstFacade::GetRenderNumBoneInfluences() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderNumBoneInfluences(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<TArray<int32>> FCollectionClothLodConstFacade::GetRenderBoneIndices() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderBoneIndices(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	TConstArrayView<TArray<float>> FCollectionClothLodConstFacade::GetRenderBoneWeights() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderBoneWeights(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			LodIndex);
	}

	int32 FCollectionClothLodConstFacade::GetNumRenderFaces() const
	{
		return ClothCollection->GetNumSubElements(
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderFacesStart(),
			ClothCollection->GetRenderFacesEnd(),
			LodIndex);
	}

	TConstArrayView<FIntVector3> FCollectionClothLodConstFacade::GetRenderIndices() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderIndices(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderFacesStart(),
			ClothCollection->GetRenderFacesEnd(),
			LodIndex);
	}

	TConstArrayView<int32> FCollectionClothLodConstFacade::GetRenderMaterialIndex() const
	{
		return ClothCollection->GetSubElements(
			ClothCollection->GetRenderMaterialIndex(),
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetRenderFacesStart(),
			ClothCollection->GetRenderFacesEnd(),
			LodIndex);
	}

	TConstArrayView<float> FCollectionClothLodConstFacade::GetWeightMap(const FName& Name) const
	{
		const TManagedArray<float>* const WeightMap = ClothCollection->GetUserDefinedAttribute<float>(Name, FClothCollection::SimVerticesGroup);
		return ClothCollection->GetSubElements(
			WeightMap,
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			LodIndex);
	}

	void FCollectionClothLodConstFacade::BuildSimulationMesh(TArray<FVector3f>& Positions, TArray<FVector3f>& Normals, TArray<uint32>& Indices, TArray<int32>& WeldingMap) const
	{
		const int32 NumSimVertices = GetNumSimVertices();

		// Initialize welding map with same index
		// The welding map redirects to an existing vertex index if these two are part of the same welding group.
		// The redirected index must be the smallest index in the group.

		WeldingMap.SetNumUninitialized(NumSimVertices);
		for (int32 SimVertexIndex = 0; SimVertexIndex < NumSimVertices; ++SimVertexIndex)
		{
			WeldingMap[SimVertexIndex] = SimVertexIndex;
		}

		// Define welding groups
		// Welding groups contain all stitched pair of indices to be welded together that are required to build the welding map.
		// Key is the smallest redirected index in the group, and will be the one index used in the welding map redirects.
		TMap<int32, TSet<int32>> WeldingGroups;

		auto UpdateWeldingMap =
			[&WeldingMap, &WeldingGroups](int32 Index0, int32 Index1)
		{
			// Only process pairs that are not already redirected to the same index
			if (WeldingMap[Index0] != WeldingMap[Index1])
			{
				// Make sure Index0 points to the the smallest redirected index, so that merges are done into the correct group
				if (WeldingMap[Index0] > WeldingMap[Index1])
				{
					Swap(Index0, Index1);
				}

				// Find the group for Index0 if any
				const int32 Key0 = WeldingMap[Index0];
				TSet<int32>* WeldingGroup0 = WeldingGroups.Find(Key0);
				if (!WeldingGroup0)
				{
					// No existing group, create a new one
					check(Key0 == Index0);  // No group means this index can't already have been redirected  // TODO: Make this a checkSlow
					WeldingGroup0 = &WeldingGroups.Add(Key0);
					WeldingGroup0->Add(Index0);
				}

				// Find the group for Index1, if it exists merge the two groups
				const int32 Key1 = WeldingMap[Index1];
				if (TSet<int32>* const WeldingGroup1 = WeldingGroups.Find(Key1))
				{
					// Update group1 redirected indices with the new key
					for (int32 Index : *WeldingGroup1)
					{
						WeldingMap[Index] = Key0;
					}

					// Merge group0 & group1
					WeldingGroup0->Append(*WeldingGroup1);

					// Remove group1
					WeldingGroups.Remove(Key1);

					// Sanity check
					check(WeldingGroup0->Contains(Key0) && WeldingGroup0->Contains(Key1));  // TODO: Make this a checkSlow
				}
				else
				{
					// Otherwise add Index1 to Index0's group
					check(Key1 == Index1);  // No group means this index can't already have been redirected  // TODO: Make this a checkSlow
					WeldingMap[Index1] = Key0;
					WeldingGroup0->Add(Index1);
				}
			}
		};

		// Apply all seams
		const int32 NumSeams = GetNumSeams();
		const TConstArrayView<TArray<FIntVector2>> SeamStitches = GetSeamStitches();
		for (int32 SeamIndex = 0; SeamIndex < NumSeams; ++SeamIndex)
		{
			for (const FIntVector2& Stitch : SeamStitches[SeamIndex])
			{
				UpdateWeldingMap(Stitch[0], Stitch[1]);
			}
		}

		// Calculate the number of welded vertices
		int32 NumWeldedVertices = NumSimVertices;
		for (const TPair<int32, TSet<int32>>& WeldingGroup : WeldingGroups)
		{
			NumWeldedVertices -= WeldingGroup.Value.Num() - 1;
		}

		// Fill up the vertex arrays
		Positions.SetNumUninitialized(NumWeldedVertices);
		Normals.SetNumUninitialized(NumWeldedVertices);

		const TConstArrayView<FVector3f> SimRestPosition = GetSimRestPosition();
		const TConstArrayView<FVector3f> SimRestNormal = GetSimRestNormal();

		TArray<uint32> WeldedIndices;
		WeldedIndices.SetNumUninitialized(NumSimVertices);

		uint32 WeldedIndex = 0;
		for (int32 VertexIndex = 0; VertexIndex < NumSimVertices; ++VertexIndex)
		{
			if (WeldingMap[VertexIndex] == VertexIndex)
			{
				Positions[WeldedIndex] = SimRestPosition[VertexIndex];
				Normals[WeldedIndex] = SimRestNormal[VertexIndex];
				WeldedIndices[VertexIndex] = WeldedIndex++;
			}
			else
			{
				WeldedIndices[VertexIndex] = WeldedIndices[WeldingMap[VertexIndex]];
			}
		}

		// Fill up the face array
		const int32 NumSimFaces = GetNumSimFaces();
		Indices.SetNumUninitialized(NumSimFaces * 3);

		const TConstArrayView<FIntVector3> SimIndices = GetSimIndices();

		for (int32 FaceIndex = 0; FaceIndex < NumSimFaces; ++FaceIndex)
		{
			Indices[FaceIndex * 3 + 0] = WeldedIndices[SimIndices[FaceIndex][0]];
			Indices[FaceIndex * 3 + 1] = WeldedIndices[SimIndices[FaceIndex][1]];
			Indices[FaceIndex * 3 + 2] = WeldedIndices[SimIndices[FaceIndex][2]];
		}
	}

	FCollectionClothLodConstFacade::FCollectionClothLodConstFacade(const TSharedPtr<const FClothCollection>& InClothCollection, int32 InLodIndex)
		: ClothCollection(InClothCollection)
		, LodIndex(InLodIndex)
	{
		check(ClothCollection.IsValid());
		check(ClothCollection->IsValid());
		check(LodIndex >= 0 && LodIndex < ClothCollection->GetNumElements(FClothCollection::LodsGroup));
	}

	void FCollectionClothLodFacade::Reset()
	{
		SetNumMaterials(0);
		SetNumTetherBatches(0);
		SetNumSeams(0);
		SetNumPatterns(0);
	}

	template<bool bWeldNearlyCoincidentVertices>
	void FCollectionClothLodFacade::InitializeFromDynamicMeshInternal(const UE::Geometry::FDynamicMesh3& DynamicMesh, int32 UVChannelIndex)
	{
		using namespace UE::Chaos::ClothAsset::Private;
		Reset();

		const UE::Geometry::FDynamicMeshAttributeSet* const AttributeSet = DynamicMesh.Attributes();
		const UE::Geometry::FDynamicMeshUVOverlay* const UVOverlay = AttributeSet ? AttributeSet->GetUVLayer(UVChannelIndex) : nullptr;

		TArray<FIsland> Islands;
		if (UVOverlay)
		{
			BuildIslandsFromDynamicMeshUVs(*UVOverlay, Islands);
		}
		else
		{
			UnwrapDynamicMesh<bWeldNearlyCoincidentVertices>(DynamicMesh, Islands);
		}

		for (FIsland& Island : Islands)
		{
			if (Island.Indices.Num() && Island.Positions.Num() && Island.RestPositions.Num())
			{
				FCollectionClothPatternFacade Pattern = AddGetPattern();
				Pattern.Initialize(Island.Positions, Island.RestPositions, Island.Indices);
			}
		}

		TArray<FSeam> Seams;
		BuildSeams(Islands, DynamicMesh, Seams);  // Build the seam information as to be able to re-weld the mesh for simulation

		SetNumSeams(Seams.Num());

		const TArrayView<TArray<FIntVector2>> SeamStitches = GetSeamStitches();
		const TArrayView<FIntVector2> SeamPatterns = GetSeamPatterns();
		const TManagedArray<int32>* const SimVerticesStart = ClothCollection->GetSimVerticesStart();

		for (int32 SeamIndex = 0; SeamIndex < Seams.Num(); ++SeamIndex)
		{
			const FIntVector2& Patterns = Seams[SeamIndex].Patterns;
			SeamPatterns[SeamIndex] = Patterns;

			const int32 NumStitches = Seams[SeamIndex].Stitches.Num();
			SeamStitches[SeamIndex].Reset(NumStitches);
			for (const FIntVector2& Stitch : Seams[SeamIndex].Stitches)
			{
				SeamStitches[SeamIndex].Emplace(Stitch[0] + (*SimVerticesStart)[Patterns[0]], Stitch[1] + (*SimVerticesStart)[Patterns[1]]);
			}
		}

	}

	void FCollectionClothLodFacade::Initialize(const TArray<FVector3f>& Positions, const TArray<uint32>& Indices)
	{
		using namespace UE::Chaos::ClothAsset::Private;

		// Build a DynamicMesh from Positions and Indices
		UE::Geometry::TToDynamicMeshBase<FSimpleSrcMeshInterface> ToDynamicMesh;
		FSimpleSrcMeshInterface SimpleSrc(Positions, Indices);

		UE::Geometry::FDynamicMesh3 DynamicMesh;
		ToDynamicMesh.Convert(DynamicMesh, SimpleSrc, [](FSimpleSrcMeshInterface::TriIDType) {return 0; });
		UE::Geometry::FNonManifoldMappingSupport::AttachNonManifoldVertexMappingData(ToDynamicMesh.ToSrcVertIDMap, DynamicMesh);

		InitializeFromDynamicMeshInternal<true>(DynamicMesh, INDEX_NONE);	
	}

	void FCollectionClothLodFacade::Initialize(const UE::Geometry::FDynamicMesh3& DynamicMesh, int32 UVChannelIndex)
	{
		InitializeFromDynamicMeshInternal<false>(DynamicMesh, UVChannelIndex);
	}

	void FCollectionClothLodFacade::Initialize(const FCollectionClothLodConstFacade& Other)
	{
		Reset();

		// Patterns Group
		const int32 NumPatterns = Other.GetNumPatterns();
		SetNumPatterns(NumPatterns);
		for (int32 PatternIndex = 0; PatternIndex < NumPatterns; ++PatternIndex)
		{
			GetPattern(PatternIndex).Initialize(Other.GetPattern(PatternIndex));
		}

		// Seam Group
		const int32 NumSeams = Other.GetNumSeams();
		SetNumSeams(NumSeams);
		for (int32 SeamIndex = 0; SeamIndex < NumSeams; ++SeamIndex)
		{
			GetSeamPatterns()[SeamIndex] = Other.GetSeamPatterns()[SeamIndex];
			GetSeamStitches()[SeamIndex] = Other.GetSeamStitches()[SeamIndex];
		}

		// Tether Batches Group
		const int32 NumTetherBatches = Other.GetNumPatterns();
		SetNumTetherBatches(NumTetherBatches);
		for (int32 TetherBatchIndex = 0; TetherBatchIndex < NumTetherBatches; ++TetherBatchIndex)
		{
			// GetTetherBatch(TetherBatchIndex).Initialize(Other.GetTetherBatch(TetherBatchIndex));  // TODO: Tether Batches facade
		}

		// Materials Group
		const int32 NumMaterials = Other.GetNumMaterials();
		SetNumMaterials(NumMaterials);
		for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
		{
			GetRenderMaterialPathName()[MaterialIndex] = Other.GetRenderMaterialPathName()[MaterialIndex];
		}

		// LODs Group
		SetPhysicsAssetPathName(Other.GetPhysicsAssetPathName());
		SetSkeletonAssetPathName(Other.GetSkeletonAssetPathName());
	}

	void FCollectionClothLodFacade::SetNumMaterials(int32 InNumMaterials)
	{
		GetClothCollection()->SetNumElements(
			InNumMaterials,
			FClothCollection::MaterialsGroup,
			GetClothCollection()->GetMaterialStart(),
			GetClothCollection()->GetMaterialEnd(),
			LodIndex);
	}

	void FCollectionClothLodFacade::SetNumTetherBatches(int32 InNumTetherBatches)
	{
		const int32 NumTetherBatches = GetNumTetherBatches();

		for (int32 TetherBatchIndex = InNumTetherBatches; TetherBatchIndex < NumTetherBatches; ++TetherBatchIndex)
		{
			// GetTetherBatch(TetherBatchIndex).Reset();  // TODO: Tether reset
		}

		GetClothCollection()->SetNumElements(
			InNumTetherBatches,
			FClothCollection::TetherBatchesGroup,
			GetClothCollection()->GetTetherBatchStart(),
			GetClothCollection()->GetTetherBatchEnd(),
			LodIndex);

		for (int32 TetherBatchIndex = NumTetherBatches; TetherBatchIndex < InNumTetherBatches; ++TetherBatchIndex)
		{
			// GetTetherBatch(TetherBatchIndex).SetDefaults();  // TODO: Tether set default
		}
	}

	void FCollectionClothLodFacade::SetNumSeams(int32 InNumSeams)
	{
		GetClothCollection()->SetNumElements(
			InNumSeams,
			FClothCollection::SeamsGroup,
			GetClothCollection()->GetSeamStart(),
			GetClothCollection()->GetSeamEnd(),
			LodIndex);
	}


	void FCollectionClothLodFacade::SetNumPatterns(int32 InNumPatterns)
	{
		const int32 NumPatterns = GetNumPatterns();

		for (int32 PatternIndex = InNumPatterns; PatternIndex < NumPatterns; ++PatternIndex)
		{
			GetPattern(PatternIndex).Reset();
		}

		GetClothCollection()->SetNumElements(
			InNumPatterns,
			FClothCollection::PatternsGroup,
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			LodIndex);

		for (int32 PatternIndex = NumPatterns; PatternIndex < InNumPatterns; ++PatternIndex)
		{
			GetPattern(PatternIndex).SetDefaults();
		}
	}

	TArrayView<FString> FCollectionClothLodFacade::GetRenderMaterialPathName()
	{
		return GetClothCollection()->GetElements(GetClothCollection()->GetRenderMaterialPathName(), ClothCollection->GetMaterialStart(), ClothCollection->GetMaterialEnd(), LodIndex);
	}

	TArrayView<FIntVector2> FCollectionClothLodFacade::GetSeamPatterns()
	{
		return GetClothCollection()->GetElements(GetClothCollection()->GetSeamPatterns(), ClothCollection->GetSeamStart(), ClothCollection->GetSeamEnd(), LodIndex);
	}

	TArrayView<TArray<FIntVector2>> FCollectionClothLodFacade::GetSeamStitches()
	{
		return GetClothCollection()->GetElements(GetClothCollection()->GetSeamStitches(), ClothCollection->GetSeamStart(), ClothCollection->GetSeamEnd(), LodIndex);
	}

	int32 FCollectionClothLodFacade::AddPattern()
	{
		const int32 PatternIndex = GetNumPatterns();
		SetNumPatterns(PatternIndex + 1);
		return PatternIndex;
	}

	FCollectionClothPatternFacade FCollectionClothLodFacade::GetPattern(int32 PatternIndex)
	{
		return FCollectionClothPatternFacade(GetClothCollection(), LodIndex, PatternIndex);
	}

	void FCollectionClothLodFacade::SetPhysicsAssetPathName(const FString& PhysicsAssetPathName)
	{
		(*GetClothCollection()->GetPhysicsAssetPathName())[LodIndex] = PhysicsAssetPathName;
	}

	void FCollectionClothLodFacade::SetSkeletonAssetPathName(const FString& SkeletonAssetPathName)
	{
		(*GetClothCollection()->GetSkeletonAssetPathName())[LodIndex] = SkeletonAssetPathName;
	}

	TArrayView<FVector2f> FCollectionClothLodFacade::GetSimPosition()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetSimPosition(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			LodIndex);
	}

	TArrayView<FVector3f> FCollectionClothLodFacade::GetSimRestPosition()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetSimRestPosition(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			LodIndex);
	}

	TArrayView<FVector3f> FCollectionClothLodFacade::GetSimRestNormal()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetSimRestNormal(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			LodIndex);
	}

	TArrayView<int32> FCollectionClothLodFacade::GetSimNumBoneInfluences()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetSimNumBoneInfluences(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			LodIndex);
	}

	TArrayView<TArray<int32>> FCollectionClothLodFacade::GetSimBoneIndices()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetSimBoneIndices(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			LodIndex);
	}

	TArrayView<TArray<float>> FCollectionClothLodFacade::GetSimBoneWeights()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetSimBoneWeights(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			LodIndex);
	}

	TArrayView<FIntVector3> FCollectionClothLodFacade::GetSimIndices()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetSimIndices(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetSimFacesStart(),
			GetClothCollection()->GetSimFacesEnd(),
			LodIndex);
	}

	TArrayView<FVector3f> FCollectionClothLodFacade::GetRenderPosition()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderPosition(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			LodIndex);
	}

	TArrayView<FVector3f> FCollectionClothLodFacade::GetRenderNormal()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderNormal(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			LodIndex);
	}

	TArrayView<FVector3f> FCollectionClothLodFacade::GetRenderTangentU()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderTangentU(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			LodIndex);
	}

	TArrayView<FVector3f> FCollectionClothLodFacade::GetRenderTangentV()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderTangentV(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			LodIndex);
	}

	TArrayView<TArray<FVector2f>> FCollectionClothLodFacade::GetRenderUVs()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderUVs(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			LodIndex);
	}

	TArrayView<FLinearColor> FCollectionClothLodFacade::GetRenderColor()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderColor(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			LodIndex);
	}

	TArrayView<int32> FCollectionClothLodFacade::GetRenderNumBoneInfluences()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderNumBoneInfluences(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			LodIndex);
	}

	TArrayView<TArray<int32>> FCollectionClothLodFacade::GetRenderBoneIndices()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderBoneIndices(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			LodIndex);
	}

	TArrayView<TArray<float>> FCollectionClothLodFacade::GetRenderBoneWeights()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderBoneWeights(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			LodIndex);
	}

	TArrayView<FIntVector3> FCollectionClothLodFacade::GetRenderIndices()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderIndices(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderFacesStart(),
			GetClothCollection()->GetRenderFacesEnd(),
			LodIndex);
	}

	TArrayView<int32> FCollectionClothLodFacade::GetRenderMaterialIndex()
	{
		return GetClothCollection()->GetSubElements(
			GetClothCollection()->GetRenderMaterialIndex(),
			GetClothCollection()->GetPatternStart(),
			GetClothCollection()->GetPatternEnd(),
			GetClothCollection()->GetRenderFacesStart(),
			GetClothCollection()->GetRenderFacesEnd(),
			LodIndex);
	}

	TArrayView<float> FCollectionClothLodFacade::GetWeightMap(const FName& Name)
	{
		TManagedArray<float>* const WeightMap = GetClothCollection()->GetUserDefinedAttribute<float>(Name, FClothCollection::SimVerticesGroup);
		return GetClothCollection()->GetSubElements(
			WeightMap,
			ClothCollection->GetPatternStart(),
			ClothCollection->GetPatternEnd(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			LodIndex);
	}

	FCollectionClothLodFacade::FCollectionClothLodFacade(const TSharedPtr<FClothCollection>& InClothCollection, int32 InLodIndex)
		: FCollectionClothLodConstFacade(InClothCollection, InLodIndex)
	{
	}

	void FCollectionClothLodFacade::SetDefaults()
	{
		(*GetClothCollection()->GetPatternStart())[LodIndex] = INDEX_NONE;
		(*GetClothCollection()->GetPatternEnd())[LodIndex] = INDEX_NONE;
		(*GetClothCollection()->GetSeamStart())[LodIndex] = INDEX_NONE;
		(*GetClothCollection()->GetSeamEnd())[LodIndex] = INDEX_NONE;
		(*GetClothCollection()->GetTetherBatchStart())[LodIndex] = INDEX_NONE;
		(*GetClothCollection()->GetTetherBatchEnd())[LodIndex] = INDEX_NONE;
		(*GetClothCollection()->GetMaterialStart())[LodIndex] = INDEX_NONE;
		(*GetClothCollection()->GetMaterialEnd())[LodIndex] = INDEX_NONE;
		(*GetClothCollection()->GetPhysicsAssetPathName())[LodIndex].Empty();
		(*GetClothCollection()->GetSkeletonAssetPathName())[LodIndex].Empty();
	}
}  // End namespace UE::Chaos::ClothAsset
