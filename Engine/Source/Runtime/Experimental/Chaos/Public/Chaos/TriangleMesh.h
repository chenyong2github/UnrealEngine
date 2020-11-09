// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/Particles.h"
#include "Chaos/SegmentMesh.h"
#include "Containers/ContainersFwd.h"

namespace Chaos
{
	template<class T>
	class TTriangleMesh
	{
	public:
		CHAOS_API TTriangleMesh();
		CHAOS_API TTriangleMesh(TArray<TVector<int32, 3>>&& Elements, const int32 StartIdx = 0, const int32 EndIdx = -1, const bool CullDegenerateElements=true);
		TTriangleMesh(const TTriangleMesh& Other) = delete;
		CHAOS_API TTriangleMesh(TTriangleMesh&& Other);
		CHAOS_API ~TTriangleMesh();

		/**
		 * Initialize the \c TTriangleMesh.
		 *
		 *	\p CullDegenerateElements removes faces with degenerate indices, and 
		 *	will change the order of \c MElements.
		 */
		CHAOS_API void Init(TArray<TVector<int32, 3>>&& Elements, const int32 StartIdx = 0, const int32 EndIdx = -1, const bool CullDegenerateElements=true);
		CHAOS_API void Init(const TArray<TVector<int32, 3>>& Elements, const int32 StartIdx = 0, const int32 EndIdx = -1, const bool CullDegenerateElements=true);

		CHAOS_API void ResetAuxiliaryStructures();

		/**
		 * Returns the closed interval of the smallest vertex index used by 
		 * this class, to the largest.
		 *
		 * If this mesh is empty, the second index of the range will be negative.
		 */
		CHAOS_API TVector<int32, 2> GetVertexRange() const;

		/** Returns the set of vertices used by triangles. */
		CHAOS_API TSet<int32> GetVertices() const;
		/** Returns the unique set of vertices used by this triangle mesh. */
		CHAOS_API void GetVertexSet(TSet<int32>& VertexSet) const;

		/**
		 * Extends the vertex range.
		 *
		 * Since the vertex range is built from connectivity, it won't include any 
		 * free vertices that either precede the first vertex, or follow the last.
		 */
		FORCEINLINE void ExpandVertexRange(const int32 StartIdx, const int32 EndIdx)
		{
			const TVector<int32, 2> CurrRange = GetVertexRange();
			if (StartIdx <= CurrRange[0] && EndIdx >= CurrRange[1])
			{
				MStartIdx = StartIdx;
				MNumIndices = EndIdx - StartIdx + 1;
			}
		}

		FORCEINLINE const TArray<TVector<int32, 3>>& GetElements() const& { return MElements; }
		/**
		 * Move accessor for topology array.
		 *
		 * Use via:
		 * \code
		 * TArray<TVector<int32,3>> Triangles;
		 * TTriangleMesh<T> TriMesh(Triangles); // steals Triangles to TriMesh::MElements
		 * Triangles = MoveTemp(TriMesh).GetElements(); // steals TriMesh::MElements back to Triangles
		 * \endcode
		 */
		FORCEINLINE TArray<TVector<int32, 3>> GetElements() && { return MoveTemp(MElements); }

		FORCEINLINE const TArray<TVector<int32, 3>>& GetSurfaceElements() const& { return MElements; }
		/**
		 * Move accessor for topology array.
		 *
		 * Use via:
		 * \code
		 * TArray<TVector<int32,3>> Triangles;
		 * TTriangleMesh<T> TriMesh(Triangles); // steals Triangles to TriMesh::MElements
		 * Triangles = MoveTemp(TriMesh).GetSurfaceElements(); // steals TriMesh::MElements back to Triangles
		 * \endcode
		 */
		FORCEINLINE TArray<TVector<int32, 3>> GetSurfaceElements() && { return MoveTemp(MElements); }

		FORCEINLINE int32 GetNumElements() const { return MElements.Num(); }

		CHAOS_API const TMap<int32, TSet<uint32>>& GetPointToNeighborsMap() const;
		FORCEINLINE const TSet<uint32>& GetNeighbors(const int32 Element) const { return GetPointToNeighborsMap()[Element]; }

		CHAOS_API TConstArrayView<TArray<int32>> GetPointToTriangleMap() const;  // Return an array view using global indexation. Only elements starting at MStartIdx will be valid!
		FORCEINLINE const TArray<int32>& GetCoincidentTriangles(const int32 Element) const { return GetPointToTriangleMap()[Element]; }

		FORCEINLINE TSet<int32> GetNRing(const int32 Element, const int32 N) const
		{
			TSet<int32> Neighbors;
			TSet<uint32> LevelNeighbors, PrevLevelNeighbors;
			PrevLevelNeighbors = GetNeighbors(Element);
			for (auto SubElement : PrevLevelNeighbors)
			{
				check(SubElement != Element);
				Neighbors.Add(SubElement);
			}
			for (int32 i = 1; i < N; ++i)
			{
				for (auto SubElement : PrevLevelNeighbors)
				{
					const auto& SubNeighbors = GetNeighbors(SubElement);
					for (auto SubSubElement : SubNeighbors)
					{
						if (!Neighbors.Contains(SubSubElement) && SubSubElement != Element)
						{
							LevelNeighbors.Add(SubSubElement);
						}
					}
				}
				PrevLevelNeighbors = LevelNeighbors;
				LevelNeighbors.Reset();
				for (auto SubElement : PrevLevelNeighbors)
				{
					if (!Neighbors.Contains(SubElement))
					{
						check(SubElement != Element);
						Neighbors.Add(SubElement);
					}
				}
			}
			return Neighbors;
		}

		/** Return the array of all cross segment indices for all pairs of adjacent triangles. */
		CHAOS_API TArray<Chaos::TVector<int32, 2>> GetUniqueAdjacentPoints() const;
		/** Return the array of bending element indices {i0, i1, i2, i3}, with {i0, i1} the segment indices and {i2, i3} the cross segment indices. */
		CHAOS_API TArray<Chaos::TVector<int32, 4>> GetUniqueAdjacentElements() const;

		/** The GetFaceNormals functions assume Counter Clockwise triangle windings in a Left Handed coordinate system
			If this is not the case the returned face normals may be inverted
		*/
		CHAOS_API TArray<TVector<T, 3>> GetFaceNormals(const TConstArrayView<TVector<T, 3>>& Points, const bool ReturnEmptyOnError = true) const;
		CHAOS_API void GetFaceNormals(TArray<TVector<T, 3>>& Normals, const TConstArrayView<TVector<T, 3>>& Points, const bool ReturnEmptyOnError = true) const;
		FORCEINLINE TArray<TVector<T, 3>> GetFaceNormals(const TParticles<T, 3>& InParticles, const bool ReturnEmptyOnError = true) const
		{ return GetFaceNormals(InParticles.X(), ReturnEmptyOnError); }

		CHAOS_API TArray<TVector<T, 3>> GetPointNormals(const TConstArrayView<TVector<T, 3>>& points, const bool ReturnEmptyOnError = true);
		FORCEINLINE TArray<TVector<T, 3>> GetPointNormals(const TParticles<T, 3>& InParticles, const bool ReturnEmptyOnError = true)
		{ return GetPointNormals(InParticles.X(), ReturnEmptyOnError); }

		CHAOS_API void GetPointNormals(TArrayView<TVector<T, 3>> PointNormals, const TConstArrayView<TVector<T, 3>>& FaceNormals, const bool bUseGlobalArray);
		/** \brief Get per-point normals. 
		 * This const version of this function requires \c GetPointToTriangleMap() 
		 * to be called prior to invoking this function. 
		 * @param bUseGlobalArray When true, fill the array from the StartIdx to StartIdx + NumIndices - 1 positions, otherwise fill the array from the 0 to NumIndices - 1 positions.
		 */
		CHAOS_API void GetPointNormals(TArrayView<TVector<T, 3>> PointNormals, const TConstArrayView<TVector<T, 3>>& FaceNormals, const bool bUseGlobalArray) const;

		static CHAOS_API TTriangleMesh<T> GetConvexHullFromParticles(const TConstArrayView<TVector<T, 3>>& points);
		/** Deprecated. Use TArrayView version. */
		static FORCEINLINE TTriangleMesh<T> GetConvexHullFromParticles(const TParticles<T, 3>& InParticles)
		{ return GetConvexHullFromParticles(InParticles.X()); }

		/**
		 * @ret The connectivity of this mesh represented as a collection of unique segments.
		 */
		CHAOS_API TSegmentMesh<T>& GetSegmentMesh();
		/** @ret A map from all face indices, to the indices of their associated edges. */
		CHAOS_API const TArray<TVector<int32, 3>>& GetFaceToEdges();
		/** @ret A map from all edge indices, to the indices of their containing faces. */
		CHAOS_API const TArray<TVector<int32, 2>>& GetEdgeToFaces();

		/**
		 * @ret Curvature between adjacent faces, specified on edges in radians.
		 * @param faceNormals - a normal per face.
		 * Curvature between adjacent faces is measured by the angle between face normals,
		 * where a curvature of 0 means they're coplanar.
		 */
		CHAOS_API TArray<T> GetCurvatureOnEdges(const TArray<TVector<T, 3>>& faceNormals);
		/** @brief Helper that generates face normals on the fly. */
		CHAOS_API TArray<T> GetCurvatureOnEdges(const TConstArrayView<TVector<T, 3>>& points);

		/**
		 * @ret The maximum curvature at points from connected edges, specified in radians.
		 * @param edgeCurvatures - a curvature per edge.
		 * The greater the number, the sharper the crease. -FLT_MAX denotes free particles.
		 */
		CHAOS_API TArray<T> GetCurvatureOnPoints(const TArray<T>& edgeCurvatures);
		/** @brief Helper that generates edge curvatures on the fly. */
		CHAOS_API TArray<T> GetCurvatureOnPoints(const TConstArrayView<TVector<T, 3>>& points);

		/**
		 * Get the set of point indices that live on the boundary (an edge with only 1 
		 * coincident face).
		 */
		CHAOS_API TSet<int32> GetBoundaryPoints();

		/**
		 * Find vertices that are coincident within the subset @param TestIndices 
		 * of given coordinates @param Points, and return a correspondence mapping
		 * from redundant vertex index to consolidated vertex index.
		 */
		CHAOS_API TMap<int32, int32> FindCoincidentVertexRemappings(
			const TArray<int32>& TestIndices,
			const TConstArrayView<TVector<T, 3>>& Points);

		/**
		 * @ret An array of vertex indices ordered from most important to least.
		 * @param Points - point positions.
		 * @param PointCurvatures - a per-point measure of curvature.
		 * @param CoincidentVertices - indices of points that are coincident to another point.
		 * @param RestrictToLocalIndexRange - ignores points outside of the index range used by this mesh.
		 */
		CHAOS_API TArray<int32> GetVertexImportanceOrdering(
		    const TConstArrayView<TVector<T, 3>>& Points,
		    const TArray<T>& PointCurvatures,
		    TArray<int32>* CoincidentVertices = nullptr,
		    const bool RestrictToLocalIndexRange = false);
		/** @brief Helper that generates point curvatures on the fly. */
		CHAOS_API TArray<int32> GetVertexImportanceOrdering(
		    const TConstArrayView<TVector<T, 3>>& Points,
		    TArray<int32>* CoincidentVertices = nullptr,
		    const bool RestrictToLocalIndexRange = false);

		/** @brief Reorder vertices according to @param Order. */
		CHAOS_API void RemapVertices(const TArray<int32>& Order);
		CHAOS_API void RemapVertices(const TMap<int32, int32>& Remapping);

		CHAOS_API void RemoveDuplicateElements();
		CHAOS_API void RemoveDegenerateElements();

		static FORCEINLINE void InitEquilateralTriangleXY(TTriangleMesh<T>& TriMesh, TParticles<T, 3>& Particles)
		{
			const int32 Idx = Particles.Size();
			Particles.AddParticles(3);
			// Left handed
			Particles.X(Idx + 0) = TVector<T, 3>(0., 0.8083, 0.);
			Particles.X(Idx + 1) = TVector<T, 3>(0.7, -0.4041, 0.);
			Particles.X(Idx + 2) = TVector<T, 3>(-0.7, -0.4041, 0.);

			TArray<TVector<int32, 3>> Elements;
			Elements.SetNum(1);
			Elements[0] = TVector<int32, 3>(Idx + 0, Idx + 1, Idx + 2);

			TriMesh.Init(MoveTemp(Elements));
		}
		static FORCEINLINE void InitEquilateralTriangleYZ(TTriangleMesh<T>& TriMesh, TParticles<T, 3>& Particles)
		{
			const int32 Idx = Particles.Size();
			Particles.AddParticles(3);
			// Left handed
			Particles.X(Idx + 0) = TVector<T, 3>(0., 0., 0.8083);
			Particles.X(Idx + 1) = TVector<T, 3>(0., 0.7, -0.4041);
			Particles.X(Idx + 2) = TVector<T, 3>(0., -0.7, -0.4041);

			TArray<TVector<int32, 3>> Elements;
			Elements.SetNum(1);
			Elements[0] = TVector<int32, 3>(Idx + 0, Idx + 1, Idx + 2);

			TriMesh.Init(MoveTemp(Elements));
		}

	private:
		CHAOS_API void InitHelper(const int32 StartIdx, const int32 EndIdx, const bool CullDegenerateElements=true);

		FORCEINLINE int32 GlobalToLocal(int32 GlobalIdx) const
		{
			const int32 LocalIdx = GlobalIdx - MStartIdx;
			check(LocalIdx >= 0 && LocalIdx < MNumIndices);
			return LocalIdx;
		}

		FORCEINLINE int32 LocalToGlobal(int32 LocalIdx) const
		{
			const int32 GlobalIdx = LocalIdx + MStartIdx;
			check(GlobalIdx >= MStartIdx && GlobalIdx < MStartIdx + MNumIndices);
			return GlobalIdx;
		}

		TArray<TVector<int32, 3>> MElements;

		mutable TArray<TArray<int32>> MPointToTriangleMap;  // !! Unlike the TArrayView returned by GetPointToTriangleMap, this array starts at 0 for the point of index MStartIdx. Use GlobalToLocal to access with a global index. Note that this array's content is always indexed in global index.
		mutable TMap<int32, TSet<uint32>> MPointToNeighborsMap;

		TSegmentMesh<T> MSegmentMesh;
		TArray<TVector<int32, 3>> MFaceToEdges;
		TArray<TVector<int32, 2>> MEdgeToFaces;

		int32 MStartIdx;
		int32 MNumIndices;
	};

#ifdef __clang__
#if PLATFORM_WINDOWS
	extern template class TTriangleMesh<float>;
#else
	extern template class CHAOS_API TTriangleMesh<float>;
#endif
#else
	extern template class TTriangleMesh<float>;
#endif

}

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_TriangleMesh_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_TriangleMesh_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_TriangleMesh_ISPC_Enabled;
#endif
