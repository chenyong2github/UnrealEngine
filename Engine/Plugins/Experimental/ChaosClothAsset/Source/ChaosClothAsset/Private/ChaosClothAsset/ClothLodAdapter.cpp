// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothLodAdapter.h"

// Utility functions to unwrap a 3d sim mesh into a tailored cloth
namespace UE::Chaos::ClothAsset::Private
{
	struct FIsland
	{
		TArray<uint32> Indices;  // 3x number of triangles
		TArray<FVector2f> Positions;
		TArray<FVector3f> RestPositions;  // Same size as Positions
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

	static void UnwrapMesh(const TArray<FVector3f>& Positions, const TArray<uint32>& Indices, TArray<FIsland>& OutIslands)
	{
		OutIslands.Reset();

		ensure(Indices.Num() % 3 == 0);
		const uint32 NumTriangles = (uint32)Indices.Num() / 3;
		if (!NumTriangles)
		{
			return;
		}

		// Gather edge information
		TMap<FUintVector2, TArray<uint32>> EdgeToTriangles;
		EdgeToTriangles.Reserve(NumTriangles * 2);  // Rough estimate for the number of edges

		auto MakeSortedEdge = [](uint32 Index0, uint32 Index1)->FUintVector2
		{
			return Index0 < Index1 ? FUintVector2(Index0, Index1) : FUintVector2(Index1, Index0);
		};

		for (uint32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
		{
			const uint32 Index0 = Indices[Triangle * 3 + 0];
			const uint32 Index1 = Indices[Triangle * 3 + 1];
			const uint32 Index2 = Indices[Triangle * 3 + 2];

			const FUintVector2 Edge0 = MakeSortedEdge(Index0, Index1);
			const FUintVector2 Edge1 = MakeSortedEdge(Index1, Index2);
			const FUintVector2 Edge2 = MakeSortedEdge(Index2, Index0);

			EdgeToTriangles.FindOrAdd(Edge0).Add(Triangle);
			EdgeToTriangles.FindOrAdd(Edge1).Add(Triangle);
			EdgeToTriangles.FindOrAdd(Edge2).Add(Triangle);
		}

		// Build pattern islands
		TSet<uint32> VisitedTriangles;
		VisitedTriangles.Reserve(NumTriangles);

		constexpr float SquaredWeldingDistance = FMath::Square(0.01f);  // 0.1 mm

		for (uint32 SeedTriangle = 0; SeedTriangle < NumTriangles; ++SeedTriangle)
		{
			if (VisitedTriangles.Contains(SeedTriangle))
			{
				continue;
			}

			const uint32 SeedIndex0 = Indices[SeedTriangle * 3 + 0];
			const uint32 SeedIndex1 = Indices[SeedTriangle * 3 + 1];

			if (FVector3f::DistSquared(Positions[SeedIndex0], Positions[SeedIndex1]) <= SquaredWeldingDistance)
			{
				continue;  // A degenerated triangle edge is not a good start
			}

			// Setup first visitor from seed, and add the first two points
			FIsland& Island = OutIslands.AddDefaulted_GetRef();

			Island.RestPositions.Add(Positions[SeedIndex0]);
			Island.RestPositions.Add(Positions[SeedIndex1]);

			const uint32 SeedIndex2D0 = Island.Positions.Add(FVector2f::ZeroVector);
			const uint32 SeedIndex2D1 = Island.Positions.Add(FVector2f(FVector3f::Dist(Positions[SeedIndex0], Positions[SeedIndex1]), 0.f));

			struct FVisitor
			{
				uint32 Triangle;
				FUintVector2 OldEdge;
				FUintVector2 NewEdge;
				uint32 CrossEdgePoint;  // Keep the opposite point to orientate degenerate cases
			} Visitor =
			{
				SeedTriangle,
				FUintVector2(SeedIndex0, SeedIndex1),
				FUintVector2(SeedIndex2D0, SeedIndex2D1),
				(uint32)INDEX_NONE
			};

			VisitedTriangles.Add(SeedTriangle);

			TQueue<FVisitor> Visitors;
			do
			{
				const uint32 Triangle = Visitor.Triangle;
				const uint32 CrossEdgePoint = Visitor.CrossEdgePoint;
				const uint32 OldIndex0 = Visitor.OldEdge.X;
				const uint32 OldIndex1 = Visitor.OldEdge.Y;
				const uint32 NewIndex0 = Visitor.NewEdge.X;
				const uint32 NewIndex1 = Visitor.NewEdge.Y;

				// Find opposite index from this triangle edge
				const uint32 TriangleIndex0 = Indices[Triangle * 3 + 0];
				const uint32 TriangleIndex1 = Indices[Triangle * 3 + 1];
				const uint32 TriangleIndex2 = Indices[Triangle * 3 + 2];

				const uint32 OldIndex2 =
					(OldIndex0 != TriangleIndex0 && OldIndex1 != TriangleIndex0) ? TriangleIndex0 :
					(OldIndex0 != TriangleIndex1 && OldIndex1 != TriangleIndex1) ? TriangleIndex1 : TriangleIndex2;

				// Find the 2D interesection of the two connecting adjacent edges using the 3D reference length
				const FVector3f& P0 = Positions[OldIndex0];
				const FVector3f& P1 = Positions[OldIndex1];
				const FVector3f& P2 = Positions[OldIndex2];

				const float R0 = FVector3f::Dist(P0, P2);
				const float R1 = FVector3f::Dist(P1, P2);
				const FVector2f C0 = Island.Positions[NewIndex0];
				const FVector2f C1 = Island.Positions[NewIndex1];

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
				uint32 NewIndex2 = INDEX_NONE;
				for (uint32 UsedIndex = 0; UsedIndex < (uint32)Island.Positions.Num(); ++UsedIndex)
				{
					if (FVector2f::DistSquared(Island.Positions[UsedIndex], C2) <= SquaredWeldingDistance &&
						FVector3f::DistSquared(Island.RestPositions[UsedIndex], Positions[OldIndex2]) <= SquaredWeldingDistance)
					{
						NewIndex2 = UsedIndex;  // Both Rest and 2D positions match, reuse this index
						break;
					}
				}
				if (NewIndex2 == INDEX_NONE)
				{
					NewIndex2 = Island.Positions.Add(C2);
					Island.RestPositions.Add(Positions[OldIndex2]);
				}

				// Add triangle to list of indices, unless it is degenerated to a segment
				if (NewIndex0 != NewIndex1 && NewIndex1 != NewIndex2 && NewIndex2 != NewIndex0)
				{
					Island.Indices.Add(NewIndex0);
					Island.Indices.Add(NewIndex1);
					Island.Indices.Add(NewIndex2);
				}

				// Add neighbor triangles to the queue
				const FUintVector2 OldEdgeList[3] =
				{
					FUintVector2(OldIndex1, OldIndex0),  // Reversed as to keep the correct winding order
					FUintVector2(OldIndex2, OldIndex1),
					FUintVector2(OldIndex0, OldIndex2)
				};
				const FUintVector3 NewEdgeList[3] =
				{
					FUintVector3(NewIndex1, NewIndex0, NewIndex2),  // Adds opposite point index
					FUintVector3(NewIndex2, NewIndex1, NewIndex0),
					FUintVector3(NewIndex0, NewIndex2, NewIndex1)
				};
				for (int32 Edge = 0; Edge < 3; ++Edge)
				{
					const uint32 EdgeIndex0 = OldEdgeList[Edge].X;
					const uint32 EdgeIndex1 = OldEdgeList[Edge].Y;

					const TArray<uint32>& NeighborTriangles = EdgeToTriangles.FindChecked(MakeSortedEdge(EdgeIndex0, EdgeIndex1));

					for (const uint32 NeighborTriangle : NeighborTriangles)
					{
						if (!VisitedTriangles.Contains(NeighborTriangle))
						{
							// Mark neighboring triangle as visited
							VisitedTriangles.Add(NeighborTriangle);

							// Enqueue next triangle
							Visitors.Enqueue(FVisitor
								{
									NeighborTriangle,
									FUintVector2(EdgeIndex0, EdgeIndex1),
									FUintVector2(NewEdgeList[Edge].X, NewEdgeList[Edge].Y),
									NewEdgeList[Edge].Z,  // Pass the cross edge 2D opposite point to help define orientation of any degenerated triangles
								});
						}
					}
				}
			} while (Visitors.Dequeue(Visitor));
		}
	}
}  // End namespace UE::ClothAsset::Private

// Cloth LOD adapter
namespace UE::Chaos::ClothAsset
{
	FClothLodConstAdapter::FClothLodConstAdapter(const TSharedPtr<const FClothCollection>& InClothCollection, int32 InLodIndex)
		: ClothCollection(InClothCollection)
		, LodIndex(InLodIndex)
	{
		check(ClothCollection.IsValid());
		check(LodIndex >= 0 && LodIndex < ClothCollection->NumElements(FClothCollection::LodsGroup));
	}

	FClothLodConstAdapter::FClothLodConstAdapter(const FClothPatternConstAdapter& ClothPatternConstAdapter)
		: ClothCollection(ClothPatternConstAdapter.GetClothCollection())
		, LodIndex(ClothPatternConstAdapter.GetLodIndex())
	{
	}

	FClothPatternConstAdapter FClothLodConstAdapter::GetPattern(int32 PatternIndex) const
	{
		return FClothPatternConstAdapter(ClothCollection, LodIndex, PatternIndex);
	}

	int32 FClothLodConstAdapter::GetNumElements(const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const
	{
		const int32 ElementIndex = GetElementIndex();
		const int32 Start = StartArray[ElementIndex];
		const int32 End = EndArray[ElementIndex];
		check(Start != INDEX_NONE || End == INDEX_NONE);  // Best to avoid situations where only one boundary of the range is set to INDEX_NONE

		return Start == INDEX_NONE ? 0 : End - Start + 1;
	}

	template<typename T>
	TConstArrayView<T> FClothLodConstAdapter::GetElements(const TManagedArray<T>& ElementArray, const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const
	{
		const int32 ElementIndex = GetElementIndex();
		const int32 Start = StartArray[ElementIndex];
		const int32 End = EndArray[ElementIndex];
		check(Start != INDEX_NONE || End == INDEX_NONE);  // Best to avoid situations where only one boundary of the range is set to INDEX_NONE

		return Start == INDEX_NONE ? TConstArrayView<T>() : TConstArrayView<T>(ElementArray.GetData() + Start, End - Start + 1);
	}

	template<bool bStart, bool bEnd>
	TTuple<int32, int32> FClothLodConstAdapter::GetPatternsElementsStartEnd(const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const
	{
		const int32 ElementIndex = GetElementIndex();
		const int32 PatternStart = ClothCollection->PatternStart[ElementIndex];
		const int32 PatternEnd = ClothCollection->PatternEnd[ElementIndex];

		// Find Start and End indices for the entire LOD minding empty patterns on the way
		int32 Start = INDEX_NONE;
		int32 End = INDEX_NONE;
		for (int32 PatternIndex = PatternStart; PatternIndex <= PatternEnd; ++PatternIndex)
		{
			if (bStart && StartArray[PatternIndex] != INDEX_NONE)
			{
				Start = (Start == INDEX_NONE) ? StartArray[PatternIndex] : FMath::Min(Start, StartArray[PatternIndex]);
			}
			if (bEnd && EndArray[PatternIndex] != INDEX_NONE)
			{
				End = (End == INDEX_NONE) ? EndArray[PatternIndex] : FMath::Max(End, EndArray[PatternIndex]);
			}
		}
		return TTuple<int32, int32>(Start, End);
	}

	int32 FClothLodConstAdapter::GetPatternsNumElements(const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const
	{
		const TTuple<int32, int32> StartEnd = GetPatternsElementsStartEnd<true, true>(StartArray, EndArray);
		const int32 Start = StartEnd.Get<0>();
		const int32 End = StartEnd.Get<1>();
		check(Start != INDEX_NONE || End == INDEX_NONE);  // Best to avoid situations where only one boundary of the range is set to INDEX_NONE

		return Start == INDEX_NONE ? 0 : End - Start + 1;
	}

	template<typename T>
	TConstArrayView<T> FClothLodConstAdapter::GetPatternsElements(const TManagedArray<T>& ElementArray, const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const
	{
		const TTuple<int32, int32> StartEnd = GetPatternsElementsStartEnd<true, true>(StartArray, EndArray);
		const int32 Start = StartEnd.Get<0>();
		const int32 End = StartEnd.Get<1>();

		check(Start != INDEX_NONE || End == INDEX_NONE);  // Best to avoid situations where only one boundary of the range is set to INDEX_NONE
		return Start == INDEX_NONE ? TConstArrayView<T>() : TConstArrayView<T>(ElementArray.GetData() + Start, End - Start + 1);
	}

	FClothLodAdapter::FClothLodAdapter(const TSharedPtr<FClothCollection>& InClothCollection, int32 InLodIndex)
		: FClothLodConstAdapter(InClothCollection, InLodIndex)
	{
	}

	int32 FClothLodAdapter::AddPattern()
	{
		const int32 PatternElementIndex = GetClothCollection()->AddElements(1, FClothCollection::PatternsGroup);

		GetClothCollection()->PatternEnd[GetElementIndex()] = PatternElementIndex;

		// If this is the first pattern being added, set also the start
		int32& PatternStart = GetClothCollection()->PatternStart[GetElementIndex()];
		if (PatternStart == INDEX_NONE)
		{
			PatternStart = (GetLodIndex() > 0) ? GetClothCollection()->PatternEnd[GetLodIndex() - 1] + 1 : 0;
		}

		const int32 PatternIndex = PatternElementIndex - PatternStart;

		GetPattern(PatternIndex).SetDefaults();

		return PatternIndex;
	}

	FClothPatternAdapter FClothLodAdapter::GetPattern(int32 PatternIndex)
	{
		return FClothPatternAdapter(GetClothCollection(), GetLodIndex(), PatternIndex);
	}

	void FClothLodAdapter::Reset()
	{
		const int32 ElementIndex = GetElementIndex();

		const int32 NumPatterns = GetNumPatterns();
		for (int32 PatternIndex = 0; PatternIndex < NumPatterns; ++PatternIndex)
		{
			GetPattern(PatternIndex).Reset();
		}
		GetClothCollection()->RemoveElements(FClothCollection::PatternsGroup, NumPatterns, GetClothCollection()->PatternStart[ElementIndex]);

		const int32 NumStitchings = GetNumStitchings();
		for (int32 StitchingIndex = 0; StitchingIndex < NumStitchings; ++StitchingIndex)
		{
			//GetStitching(StitchingIndex).Reset();  // TODO
		}
		GetClothCollection()->RemoveElements(FClothCollection::StitchingsGroup, NumStitchings, GetClothCollection()->StitchingStart[ElementIndex]);

		const int32 NumTetherBatches = GetNumTetherBatches();
		for (int32 TetherBatchIndex = 0; TetherBatchIndex < NumTetherBatches; ++TetherBatchIndex)
		{
			//TetherBatch(TetherBatchIndex).Reset();  // TODO
		}
		GetClothCollection()->RemoveElements(FClothCollection::TetherBatchesGroup, NumTetherBatches, GetClothCollection()->TetherBatchStart[ElementIndex]);

		SetDefaults();
	}

	void FClothLodAdapter::SetDefaults()
	{
		const int32 ElementIndex = GetElementIndex();

		GetClothCollection()->PatternStart[ElementIndex] = INDEX_NONE;
		GetClothCollection()->PatternEnd[ElementIndex] = INDEX_NONE;
		GetClothCollection()->StitchingStart[ElementIndex] = INDEX_NONE;
		GetClothCollection()->StitchingEnd[ElementIndex] = INDEX_NONE;
		GetClothCollection()->TetherBatchStart[ElementIndex] = INDEX_NONE;
		GetClothCollection()->TetherBatchEnd[ElementIndex] = INDEX_NONE;
		GetClothCollection()->LodBiasDepth[ElementIndex] = 0;
	}

	template<typename T>
	TArrayView<T> FClothLodAdapter::GetPatternsElements(TManagedArray<T>& ElementArray, const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray)
	{
		const TTuple<int32, int32> StartEnd = GetPatternsElementsStartEnd<true, true>(StartArray, EndArray);
		const int32 Start = StartEnd.Get<0>();
		const int32 End = StartEnd.Get<1>();

		check(Start != INDEX_NONE || End == INDEX_NONE);  // Best to avoid situations where only one boundary of the range is set to INDEX_NONE
		return Start == INDEX_NONE ? TArrayView<T>() : TArrayView<T>(ElementArray.GetData() + Start, End - Start + 1);
	}

	void FClothLodAdapter::Initialize(const TArray<FVector3f>& Positions, const TArray<uint32>& Indices)
	{
		using namespace UE::Chaos::ClothAsset::Private;

		TArray<FIsland> Islands;
		UnwrapMesh(Positions, Indices, Islands);  // Unwrap to 2D and reconstruct indices on 3D mesh

		for (FIsland& Island : Islands)
		{
			if (Island.Indices.Num() && Island.Positions.Num() && Island.RestPositions.Num())
			{
				FClothPatternAdapter Pattern = AddGetPattern();
				Pattern.Initialize(Island.Positions, Island.RestPositions, Island.Indices);
			}
		}
	}
}  // End namespace UE::Chaos::ClothAsset
