// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/Particles.h"
#include "Chaos/SegmentMesh.h"

#include <unordered_set>

namespace Chaos
{
	template<class T>
	class CHAOS_API TTriangleMesh
	{
	public:
		TTriangleMesh();
		TTriangleMesh(TArray<TVector<int32, 3>>&& Elements, const int32 StartIdx = 0, const int32 EndIdx = -1);
		TTriangleMesh(const TTriangleMesh& Other) = delete;
		TTriangleMesh(TTriangleMesh&& Other);
		~TTriangleMesh();

		void Init(TArray<TVector<int32, 3>>&& Elements, const int32 StartIdx = 0, const int32 EndIdx = -1);
		void Init(const TArray<TVector<int32, 3>>& Elements, const int32 StartIdx = 0, const int32 EndIdx = -1);

		/**
		 * Returns the closed interval of the smallest vertex index used by 
		 * this class, to the largest.
		 *
		 * If this mesh is empty, the second index of the range will be negative.
		 */
		TPair<int32, int32> GetVertexRange() const;

		/** Returns the set of vertices used by triangles. */
		TSet<int32> GetVertices() const;
		/** Returns the unique set of vertices used by this triangle mesh. */
		void GetVertexSet(TSet<int32>& VertexSet) const;

		/**
		 * Extends the vertex range.
		 *
		 * Since the vertex range is built from connectivity, it won't include any 
		 * free vertices that either precede the first vertex, or follow the last.
		 */
		void ExpandVertexRange(const int32 StartIdx, const int32 EndIdx)
		{
			const TPair<int32, int32> CurrRange = GetVertexRange();
			if (StartIdx <= CurrRange.Key && EndIdx >= CurrRange.Value)
			{
				MStartIdx = StartIdx;
				MNumIndices = EndIdx - StartIdx + 1;
			}
		}

		const TArray<TVector<int32, 3>>& GetElements() const& { return MElements; }
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
		TArray<TVector<int32, 3>> GetElements() && { return MoveTemp(MElements); }

		const TArray<TVector<int32, 3>>& GetSurfaceElements() const& { return MElements; }
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
		TArray<TVector<int32, 3>> GetSurfaceElements() && { return MoveTemp(MElements); }

		int32 GetNumElements() const { return MElements.Num(); }

		const TMap<int32, TSet<uint32>>& GetPointToNeighborsMap() const;
		const TSet<uint32>& GetNeighbors(const int32 Element) { return GetPointToNeighborsMap()[Element]; }

		const TMap<int32, TArray<int32>>& GetPointToTriangleMap() const;
		const TArray<int32>& GetCoincidentTriangles(const int32 Element) { return GetPointToTriangleMap()[Element]; }

		TSet<int32> GetNRing(const int32 Element, const int32 N)
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

		TArray<Chaos::TVector<int32, 2>> GetUniqueAdjacentPoints() const;
		TArray<Chaos::TVector<int32, 4>> GetUniqueAdjacentElements() const;

		/** The GetFaceNormals functions assume Counter Clockwise triangle windings in a Left Handed coordinate system
			If this is not the case the returned face normals may be inverted
		*/
		TArray<TVector<T, 3>> GetFaceNormals(const TArrayView<const TVector<T, 3>>& Points, const bool ReturnEmptyOnError = true) const;
		void GetFaceNormals(TArray<TVector<T, 3>>& Normals, const TArrayView<const TVector<T, 3>>& Points, const bool ReturnEmptyOnError = true) const;
		/** Deprecated. Use TArrayView version. */
		TArray<TVector<T, 3>> GetFaceNormals(const TParticles<T, 3>& InParticles, const bool ReturnEmptyOnError = true) const
		{ return GetFaceNormals(InParticles.X(), ReturnEmptyOnError); }

		TArray<TVector<T, 3>> GetPointNormals(const TArrayView<const TVector<T, 3>>& points, const bool bReturnEmptyOnError = false, const bool bUseGlobalArray = true);
		void GetPointNormals(TArray<TVector<T, 3>>& PointNormals, const TArray<TVector<T, 3>>& FaceNormals, const bool ReturnEmptyOnError = false, const bool bUseGlobalArray = true);
		/** \brief Get per-point normals. 
		 * This const version of this function requires \c GetPointToTriangleMap() 
		 * to be called prior to invoking this function. 
		 * @param bUseGlobalArray When true, fill the array from the StartIdx to StartIdx + NumIndices - 1 positions, otherwise fill the array from the 0 to NumIndices - 1 positions.
		 */
		void GetPointNormals(TArray<TVector<T, 3>>& PointNormals, const TArray<TVector<T, 3>>& FaceNormals, const bool bReturnEmptyOnError, const bool bUseGlobalArray) const;
		/** Deprecated. Use TArrayView version. */
		TArray<TVector<T, 3>> GetPointNormals(const TParticles<T, 3>& InParticles, const bool bReturnEmptyOnError = false, const bool bUseGlobalArray = true)
		{ return GetPointNormals(InParticles.X(), bReturnEmptyOnError, bUseGlobalArray); }

		static TTriangleMesh<T> GetConvexHullFromParticles(const TArrayView<const TVector<T, 3>>& points);
		/** Deprecated. Use TArrayView version. */
		static TTriangleMesh<T> GetConvexHullFromParticles(const TParticles<T, 3>& InParticles)
		{ return GetConvexHullFromParticles(InParticles.X()); }

		/**
		 * @ret The connectivity of this mesh represented as a collection of unique segments.
		 */
		TSegmentMesh<T>& GetSegmentMesh();
		/** @ret A map from all face indices, to the indices of their associated edges. */
		const TArray<TVector<int32, 3>>& GetFaceToEdges();
		/** @ret A map from all edge indices, to the indices of their containing faces. */
		const TArray<TVector<int32, 2>>& GetEdgeToFaces();

		/**
		 * @ret Curvature between adjacent faces, specified on edges in radians.
		 * @param faceNormals - a normal per face.
		 * Curvature between adjacent faces is measured by the angle between face normals,
		 * where a curvature of 0 means they're coplanar.
		 */
		TArray<T> GetCurvatureOnEdges(const TArray<TVector<T, 3>>& faceNormals);
		/** @brief Helper that generates face normals on the fly. */
		TArray<T> GetCurvatureOnEdges(const TArrayView<const TVector<T, 3>>& points);

		/**
		 * @ret The maximum curvature at points from connected edges, specified in radians.
		 * @param edgeCurvatures - a curvature per edge.
		 * The greater the number, the sharper the crease. -FLT_MAX denotes free particles.
		 */
		TArray<T> GetCurvatureOnPoints(const TArray<T>& edgeCurvatures);
		/** @brief Helper that generates edge curvatures on the fly. */
		TArray<T> GetCurvatureOnPoints(const TArrayView<const TVector<T, 3>>& points);

		/**
		 * @ret An array of vertex indices ordered from most important to least.
		 * @param Points - point positions.
		 * @param PointCurvatures - a per-point measure of curvature.
		 * @param CoincidentVertices - indices of points that are coincident to another point.
		 * @param RestrictToLocalIndexRange - ignores points outside of the index range used by this mesh.
		 */
		TArray<int32> GetVertexImportanceOrdering(
		    const TArrayView<const TVector<T, 3>>& Points,
		    const TArray<T>& PointCurvatures,
		    TArray<int32>* CoincidentVertices = nullptr,
		    const bool RestrictToLocalIndexRange = false);
		/** @brief Helper that generates point curvatures on the fly. */
		TArray<int32> GetVertexImportanceOrdering(
		    const TArrayView<const TVector<T, 3>>& Points,
		    TArray<int32>* CoincidentVertices = nullptr,
		    const bool RestrictToLocalIndexRange = false);

		/** @brief Reorder vertices according to @param Order. */
		void RemapVertices(const TArray<int32>& Order);

		static void InitEquilateralTriangleXY(TTriangleMesh<T>& TriMesh, TParticles<T, 3>& Particles)
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
		static void InitEquilateralTriangleYZ(TTriangleMesh<T>& TriMesh, TParticles<T, 3>& Particles)
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
		void InitHelper(const int32 StartIdx, const int32 EndIdx);

		int32 GlobalToLocal(int32 GlobalIdx) const
		{
			const int32 LocalIdx = GlobalIdx - MStartIdx;
			check(LocalIdx >= 0 && LocalIdx < MNumIndices);
			return LocalIdx;
		}

		TArray<TVector<int32, 3>> MElements;

		mutable TMap<int32, TArray<int32>> MPointToTriangleMap;
		mutable TMap<int32, TSet<uint32>> MPointToNeighborsMap;

		TSegmentMesh<T> MSegmentMesh;
		TArray<TVector<int32, 3>> MFaceToEdges;
		TArray<TVector<int32, 2>> MEdgeToFaces;

		int32 MStartIdx;
		int32 MNumIndices;
	};
}
