// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "ChaosArchive.h"
#include "ChaosCheck.h"
#include "ChaosLog.h"
#include "Math/NumericLimits.h"
#include "UObject/PhysicsObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

namespace Chaos
{
	// Default convex structure index traits - assumes signed
	template<typename T_INDEX>
	struct TConvexStructureIndexTraits
	{
		using FIndex = T_INDEX;
		static const FIndex InvalidIndex = TNumericLimits<FIndex>::Lowest();
		static const FIndex MaxIndex = TNumericLimits<FIndex>::Max();

		static_assert(TIsSigned<T_INDEX>::Value, "The default TConvexStructureIndexTraits implementation is only valid for signed T_INDEX");
	};

	// uint8 uses 255 as InvalidIndex, and therefore supports elements with indices 0...254
	template<>
	struct TConvexStructureIndexTraits<uint8>
	{
		using FIndex = uint8;
		static const FIndex InvalidIndex = TNumericLimits<FIndex>::Max();
		static const FIndex MaxIndex = TNumericLimits<FIndex>::Max() - 1;
	};

	// Convex half-edge structure data. Supports different index sizes.
	// Uses indices into packed arrays rather than pointers. Avoids prev/next indices by keeping a plane's edges in order and sequential.
	template<typename T_INDEX>
	class TConvexHalfEdgeStructureData
	{
	public:
		using FIndex = T_INDEX;
		using FIndexTraits = TConvexStructureIndexTraits<T_INDEX>;
		using FConvexHalfEdgeStructureData = TConvexHalfEdgeStructureData<T_INDEX>;

		static const FIndex InvalidIndex = FIndexTraits::InvalidIndex;
		static const int32 MaxIndex = (int32)FIndexTraits::MaxIndex;

		friend class FVertexPlaneIterator;

		// A plane of a convex hull. Each plane has an array of half edges, stored
		// as an index into the edge list and a count.
		struct FPlaneData
		{
			FIndex FirstHalfEdgeIndex;	// index into HalfEdges
			FIndex NumHalfEdges;

			friend FArchive& operator<<(FArchive& Ar, FPlaneData& Value)
			{
				return Ar << Value.FirstHalfEdgeIndex << Value.NumHalfEdges;
			}
		};

		// Every plane is bounded by a sequence of edges, and every edge should be shared 
		// by two planes. The edges that bound a plane are stored as a sequence of half-edges. 
		// Each half-edge references the starting vertex of the edge, and the half-edge 
		// pointing in the opposite direction (belonging to the plane that shares the edge).
		struct FHalfEdgeData
		{
			FIndex PlaneIndex;			// index into Planes
			FIndex VertexIndex;			// index into Vertices
			FIndex TwinHalfEdgeIndex;	// index into HalfEdges

			friend FArchive& operator<<(FArchive& Ar, FHalfEdgeData& Value)
			{
				return Ar << Value.PlaneIndex << Value.VertexIndex << Value.TwinHalfEdgeIndex;
			}
		};

		// A vertex of a convex hull. We just store one edge that uses the vertex - the others
		// can be found via the half-edge links.
		struct FVertexData
		{
			FIndex FirstHalfEdgeIndex;	// index into HalfEdges

			friend FArchive& operator<<(FArchive& Ar, FVertexData& Value)
			{
				return Ar << Value.FirstHalfEdgeIndex;
			}
		};

		// Initialize the structure data from the array of vertex indices per plane (in CW or CCW order - it is retained in structure)
		// If this fails for some reason, the structure data will be invalid (check IsValid())
		static FConvexHalfEdgeStructureData MakePlaneVertices(const TArray<TArray<int32>>& InPlaneVertices, int32 InNumVertices)
		{
			FConvexHalfEdgeStructureData StructureData;
			StructureData.SetPlaneVertices(InPlaneVertices, InNumVertices);
			return StructureData;
		}

		// Return true if we can support this convex, based on number of features and maximum index size
		static bool CanMake(const TArray<TArray<int32>>& InPlaneVertices, int32 InNumVertices)
		{
			int32 HalfEdgeCount = 0;
			for (int32 PlaneIndex = 0; PlaneIndex < InPlaneVertices.Num(); ++PlaneIndex)
			{
				HalfEdgeCount += InPlaneVertices[PlaneIndex].Num();
			}

			// For a well-formed convex HalfEdgeCount must be larger than NumPlanes and NumVerts, but check them all anyway just in case...
			return ((HalfEdgeCount <= MaxIndex) && (InPlaneVertices.Num() <= MaxIndex) && (InNumVertices <= MaxIndex));
		}


		bool IsValid() const { return Planes.Num() > 0; }
		int32 NumPlanes() const { return Planes.Num(); }
		int32 NumHalfEdges() const { return HalfEdges.Num(); }
		int32 NumVertices() const { return Vertices.Num(); }

		FPlaneData& GetPlane(int32 PlaneIndex) { return Planes[PlaneIndex]; }
		const FPlaneData& GetPlane(int32 PlaneIndex) const { return Planes[PlaneIndex]; }
		FHalfEdgeData& GetHalfEdge(int32 EdgeIndex) { return HalfEdges[EdgeIndex]; }
		const FHalfEdgeData& GetHalfEdge(int32 EdgeIndex) const { return HalfEdges[EdgeIndex]; }
		FVertexData& GetVertex(int32 VertexIndex) { return Vertices[VertexIndex]; }
		const FVertexData& GetVertex(int32 VertexIndex) const { return Vertices[VertexIndex]; }

		// The number of edges bounding the specified plane
		int32 NumPlaneHalfEdges(int32 PlaneIndex) const
		{
			return GetPlane(PlaneIndex).NumHalfEdges;
		}

		// The edge index of one of the bounding edges of a plane
		// PlaneIndex must be in range [0, NumPlanes())
		// PlaneEdgeIndex must be in range [0, NumPlaneHalfEdges(PlaneIndex))
		// return value is in range [0, NumHalfEdges())
		int32 GetPlaneHalfEdge(int32 PlaneIndex, int32 PlaneEdgeIndex) const
		{
			check(PlaneEdgeIndex >= 0);
			check(PlaneEdgeIndex < NumPlaneHalfEdges(PlaneIndex));
			return GetPlane(PlaneIndex).FirstHalfEdgeIndex + PlaneEdgeIndex;
		}

		// The number of vertices that bound the specified plane (same as number of half edges)
		// PlaneIndex must be in range [0, NumPlaneHalfEdges(PlaneIndex))
		int32 NumPlaneVertices(int32 PlaneIndex) const
		{
			return GetPlane(PlaneIndex).NumHalfEdges;
		}

		// Get the index of one of the vertices bounding the specified plane
		// PlaneIndex must be in range [0, NumPlanes())
		// PlaneVertexIndex must be in [0, NumPlaneVertices(PlaneIndex))
		// return value is in [0, NumVertices())
		int32 GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const
		{
			const int32 EdgeIndex = GetPlaneHalfEdge(PlaneIndex, PlaneVertexIndex);
			return GetHalfEdge(EdgeIndex).VertexIndex;
		}

		// EdgeIndex must be in range [0, NumHalfEdges())
		// return value is in range [0, NumPlanes())
		int32 GetHalfEdgePlane(int32 EdgeIndex) const
		{
			return GetHalfEdge(EdgeIndex).PlaneIndex;
		}

		// EdgeIndex must be in range [0, NumHalfEdges())
		// return value is in range [0, NumVertices())
		int32 GetHalfEdgeVertex(int32 EdgeIndex) const
		{
			return GetHalfEdge(EdgeIndex).VertexIndex;
		}

		// EdgeIndex must be in range [0, NumHalfEdges())
		// return value is in range [0, NumHalfEdges())
		int32 GetTwinHalfEdge(int32 EdgeIndex) const
		{
			return GetHalfEdge(EdgeIndex).TwinHalfEdgeIndex;
		}

		// Get the previous half edge on the same plane
		// EdgeIndex must be in range [0, NumHalfEdges())
		// return value is in range [0, NumHalfEdges())
		int32 GetPrevHalfEdge(int32 EdgeIndex) const
		{
			// Calculate the edge index on the plane
			const int32 PlaneIndex = GetHalfEdge(EdgeIndex).PlaneIndex;
			const int32 PlaneEdgeIndex = EdgeIndex - GetPlane(PlaneIndex).FirstHalfEdgeIndex;
			return GetPrevPlaneHalfEdge(PlaneIndex, PlaneEdgeIndex);
		}

		// Get the next half edge on the same plane
		// EdgeIndex must be in range [0, NumHalfEdges())
		// return value is in range [0, NumHalfEdges())
		int32 GetNextHalfEdge(int32 EdgeIndex) const
		{
			// Calculate the edge index on the plane
			const int32 PlaneIndex = GetHalfEdge(EdgeIndex).PlaneIndex;
			const int32 PlaneEdgeIndex = EdgeIndex - GetPlane(PlaneIndex).FirstHalfEdgeIndex;
			return GetNextPlaneHalfEdge(PlaneIndex, PlaneEdgeIndex);
		}

		// VertexIndex must be in range [0, NumVertices())
		// return value is in range [0, NumHalfEdges())
		int32 GetVertexFirstHalfEdge(int32 VertexIndex) const
		{
			return GetVertex(VertexIndex).FirstHalfEdgeIndex;
		}

		// Iterate over the planes assiciated with a vertex.
		// Visitor should return false to halt iteration.
		void VisitVertexPlanes(int32 VertexIndex, const TFunction<bool(int32 PlaneIndex)>& Visitor) const
		{
			const int32 FirstEdgeIndex = GetVertexFirstHalfEdge(VertexIndex);
			int32 EdgeIndex = FirstEdgeIndex;
			while (EdgeIndex != InvalidIndex)
			{
				// Send the plane to the visitor
				const int32 PlaneIndex = GetHalfEdgePlane(EdgeIndex);
				const bool bContinue = Visitor(PlaneIndex);
				
				// Stop if the vistor wants no more planes
				if (!bContinue)
				{
					break;
				}

				const int32 TwinEdgeIndex = GetTwinHalfEdge(EdgeIndex);
				if (TwinEdgeIndex == InvalidIndex)
				{
					// Malformed convex, but we need to handle it
					break;
				}

				EdgeIndex = GetNextHalfEdge(TwinEdgeIndex);
				if (EdgeIndex == FirstEdgeIndex)
				{
					// We have looped back to the first edge
					break;
				}
			}
		}

		// Fill an array with plane indices for the specified vertex. Return the number of planes found.
		int32 FindVertexPlanes(int32 VertexIndex, int32* PlaneIndices, int32 MaxVertexPlanes) const
		{
			int32 NumPlanesFound = 0;

			if (MaxVertexPlanes > 0)
			{
				VisitVertexPlanes(VertexIndex, 
					[PlaneIndices, MaxVertexPlanes, &NumPlanesFound](int32 PlaneIndex)
					{
						PlaneIndices[NumPlanesFound++] = PlaneIndex;
						return (NumPlanesFound < MaxVertexPlanes);
					});
			}

			return NumPlanesFound;
		}

		// Initialize the structure data from the set of vertices associated with each plane.
		// The vertex indices are assumed to be in CCW order (or CW order - doesn't matter here
		// as long as it is sequential).
		bool SetPlaneVertices(const TArray<TArray<int32>>& InPlaneVertices, int32 InNumVertices)
		{
			// Count the edges
			int32 HalfEdgeCount = 0;
			for (int32 PlaneIndex = 0; PlaneIndex < InPlaneVertices.Num(); ++PlaneIndex)
			{
				HalfEdgeCount += InPlaneVertices[PlaneIndex].Num();
			}

			if ((InPlaneVertices.Num() > MaxIndex) || (HalfEdgeCount > MaxIndex) || (InNumVertices > MaxIndex))
			{
				return false;
			}

			Planes.SetNum(InPlaneVertices.Num());
			HalfEdges.SetNum(HalfEdgeCount);
			Vertices.SetNum(InNumVertices);

			// Initialize the vertex list - it will be filled in as we build the edge list
			for (int32 VertexIndex = 0; VertexIndex < Vertices.Num(); ++VertexIndex)
			{
				GetVertex(VertexIndex).FirstHalfEdgeIndex = InvalidIndex;
			}

			// Build the planes and edges. The edges for a plane are stored sequentially in the half-edge array.
			// On the first pass, the edges contain 2 vertex indices, rather than a vertex index and a twin edge index.
			// We fix this up on a second pass.
			int32 NextEdgeIndex = 0;
			for (int32 PlaneIndex = 0; PlaneIndex < InPlaneVertices.Num(); ++PlaneIndex)
			{
				const TArray<int32>& PlaneVertices = InPlaneVertices[PlaneIndex];

				GetPlane(PlaneIndex) =
				{
					(FIndex)NextEdgeIndex,
					(FIndex)PlaneVertices.Num()
				};

				for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < PlaneVertices.Num(); ++PlaneVertexIndex)
				{
					// Add a new edge
					const int32 VertexIndex0 = PlaneVertices[PlaneVertexIndex];
					const int32 VertexIndex1 = PlaneVertices[(PlaneVertexIndex + 1) % PlaneVertices.Num()];
					GetHalfEdge(NextEdgeIndex) =
					{
						(FIndex)PlaneIndex,
						(FIndex)VertexIndex0,
						(FIndex)VertexIndex1,	// Will get converted to a half-edge index later
					};

					// If this is the first time Vertex0 has showed up, set its edge index
					if (Vertices[VertexIndex0].FirstHalfEdgeIndex == InvalidIndex)
					{
						Vertices[VertexIndex0].FirstHalfEdgeIndex = NextEdgeIndex;
					}

					++NextEdgeIndex;
				}
			}

			// Find the twin half edge for each edge
			// @todo(chaos): could use a map of vertex-index-pair to half edge to eliminate O(N^2) algorithm
			TArray<FIndex> TwinHalfEdgeIndices;
			TwinHalfEdgeIndices.SetNum(HalfEdges.Num());
			for (int32 EdgeIndex = 0; EdgeIndex < TwinHalfEdgeIndices.Num(); ++EdgeIndex)
			{
				TwinHalfEdgeIndices[EdgeIndex] = InvalidIndex;
			}
			for (int32 EdgeIndex0 = 0; EdgeIndex0 < HalfEdges.Num(); ++EdgeIndex0)
			{
				const int32 VertexIndex0 = HalfEdges[EdgeIndex0].VertexIndex;
				const int32 VertexIndex1 = HalfEdges[EdgeIndex0].TwinHalfEdgeIndex;	// Actually a vertex index for now...

				// Find the edge with the vertices the other way round
				for (int32 EdgeIndex1 = 0; EdgeIndex1 < HalfEdges.Num(); ++EdgeIndex1)
				{
					if ((HalfEdges[EdgeIndex1].VertexIndex == VertexIndex1) && (HalfEdges[EdgeIndex1].TwinHalfEdgeIndex == VertexIndex0))
					{
						TwinHalfEdgeIndices[EdgeIndex0] = (FIndex)EdgeIndex1;
						break;
					}
				}
			}

			// Set the twin edge indices
			for (int32 EdgeIndex = 0; EdgeIndex < HalfEdges.Num(); ++EdgeIndex)
			{
				GetHalfEdge(EdgeIndex).TwinHalfEdgeIndex = (FIndex)TwinHalfEdgeIndices[EdgeIndex];
			}

			return true;
		}

		void Serialize(FArchive& Ar)
		{
			Ar << Planes;
			Ar << HalfEdges;
			Ar << Vertices;
		}

		friend FArchive& operator<<(FArchive& Ar, FConvexHalfEdgeStructureData& Value)
		{
			Value.Serialize(Ar);
			return Ar;
		}

	private:

		// The edge index of the previous edge on the plane (loops)
		// PlaneIndex must be in range [0, NumPlanes())
		// PlaneEdgeIndex must be in range [0, NumPlaneHalfEdges(PlaneIndex))
		// return value is in range [0, NumHalfEdges())
		int32 GetPrevPlaneHalfEdge(int32 PlaneIndex, int32 PlaneEdgeIndex) const
		{
			// A plane's edges are sequential and loop
			check(PlaneEdgeIndex >= 0);
			check(PlaneEdgeIndex < NumPlaneHalfEdges(PlaneIndex));
			const int32 PlaneHalfEdgeCount = NumPlaneHalfEdges(PlaneIndex);
			const int32 PrevPlaneEdgeIndex = (PlaneEdgeIndex + PlaneHalfEdgeCount - 1) % PlaneHalfEdgeCount;
			return GetPlaneHalfEdge(PlaneIndex, PrevPlaneEdgeIndex);
		}

		// The edge index of the next edge on the plane (loops)
		// PlaneIndex must be in range [0, NumPlanes())
		// PlaneEdgeIndex must be in range [0, NumPlaneHalfEdges(PlaneIndex))
		// return value is in range [0, NumHalfEdges())
		int32 GetNextPlaneHalfEdge(int32 PlaneIndex, int32 PlaneEdgeIndex) const
		{
			// A plane's edges are sequential and loop
			check(PlaneEdgeIndex >= 0);
			check(PlaneEdgeIndex < NumPlaneHalfEdges(PlaneIndex));
			const int32 PlaneHalfEdgeCount = NumPlaneHalfEdges(PlaneIndex);
			const int32 NextPlaneEdgeIndex = (PlaneEdgeIndex + 1) % PlaneHalfEdgeCount;
			return GetPlaneHalfEdge(PlaneIndex, NextPlaneEdgeIndex);
		}

		TArray<FPlaneData> Planes;
		TArray<FHalfEdgeData> HalfEdges;
		TArray<FVertexData> Vertices;
	};


	// Typedefs for the supported index sizes
	using FConvexHalfEdgeStructureDataS32 = TConvexHalfEdgeStructureData<int32>;
	using FConvexHalfEdgeStructureDataS16 = TConvexHalfEdgeStructureData<int16>;
	using FConvexHalfEdgeStructureDataU8 = TConvexHalfEdgeStructureData<uint8>;
}
