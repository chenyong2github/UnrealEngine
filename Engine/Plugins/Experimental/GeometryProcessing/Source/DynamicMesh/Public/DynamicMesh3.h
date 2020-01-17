// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp DMesh3

#pragma once

#include "BoxTypes.h"
#include "InfoTypes.h"
#include "FrameTypes.h"
#include "GeometryTypes.h"
#include "HAL/Platform.h"
#include "MathUtil.h"
#include "Quaternion.h"
#include "Util/DynamicVector.h"
#include "Util/IndexUtil.h"
#include "Util/IteratorUtil.h"
#include "Util/RefCountVector.h"
#include "Util/SmallListSet.h"
#include "Util/CompactMaps.h"
#include "VectorTypes.h"
#include "VectorUtil.h"

class FDynamicMeshAttributeSet;
class FMeshShapeGenerator;

enum class EMeshComponents : uint8
{
	None = 0,
	VertexNormals = 1,
	VertexColors = 2,
	VertexUVs = 4,
	FaceGroups = 8,
	All = 15
};

/**
* FDynamicMesh3 is a dynamic triangle mesh class. The mesh has has connectivity,
* is an indexed mesh, and allows for gaps in the index space.
*
* internally, all data is stored in POD-type buffers, except for the vertex->edge
* links, which are stored as List<int>'s. The arrays of POD data are stored in
* TDynamicVector's, so they grow in chunks, which is relatively efficient. The actual
* blocks are arrays, so they can be efficiently mem-copied into larger buffers
* if necessary.
*
* Reference counts for verts/tris/edges are stored as separate FRefCountVector
* instances.
*
* Vertices are stored as doubles, although this should be easily changed
* if necessary, as the internal data structure is not exposed
*
* Per-vertex Vertex Normals, Colors, and UVs are optional and stored as floats.
*
* For each vertex, VertexEdgeLists[i] is the unordered list of connected edges. The
* elements of the list are indices into the edges list.
* This list is unsorted but can be traversed in-order (ie cw/ccw) at some additional cost.
*
* Triangles are stored as 3 ints, with optionally a per-triangle integer group id.
*
* The edges of a triangle are similarly stored as 3 ints, in triangle_edes. If the
* triangle is [v1,v2,v3], then the triangle edges [e1,e2,e3] are
* e1=edge(v1,v2), e2=edge(v2,v3), e3=edge(v3,v1), where the e# are indexes into edges.
*
* Edges are stored as tuples of 4 ints. If the edge is between v1 and v2, with neighbour
* tris t1 and t2, then the edge is [min(v1,v2), max(v1,v2), t1, t2]. For a boundary
* edge, t2 is InvalidID. t1 is never InvalidID.
*
* Most of the class assumes that the mesh is manifold. Many functions will
* work if the topology is non-manifold, but behavior of operators like Split/Flip/Collapse
* edge is untested.
*
* The function CheckValidity() does extensive sanity checking on the mesh data structure.
* Use this to test your code, both for mesh construction and editing!!
*
*
* TODO:
*  - Many of the iterators depend on lambda functions, can we replace these with calls to
*    internal/static functions that do the same thing?
*  - efficient TriTrianglesItr() implementation?
*  - additional Topology timestamp?
*  - CompactInPlace() does not compact VertexEdgeLists
*  ? TDynamicVector w/ 'stride' option, so that we can guarantee that tuples are in single block.
*    The can have custom accessor that looks up entire tuple
*/
class DYNAMICMESH_API FDynamicMesh3
{
public:
using FEdgeFlipInfo = DynamicMeshInfo::FEdgeFlipInfo;
using FEdgeSplitInfo = DynamicMeshInfo::FEdgeSplitInfo;
using FEdgeCollapseInfo = DynamicMeshInfo::FEdgeCollapseInfo;
using FMergeEdgesInfo = DynamicMeshInfo::FMergeEdgesInfo;
using FPokeTriangleInfo = DynamicMeshInfo::FPokeTriangleInfo;
using FVertexSplitInfo = DynamicMeshInfo::FVertexSplitInfo;

	/** InvalidID indicates that a vertex/edge/triangle ID is invalid */
	constexpr static int InvalidID = IndexConstants::InvalidID;
	/** NonManifoldID is returned by AppendTriangle() to indicate that the added triangle would result in nonmanifold geometry and hence was ignored */
	constexpr static int NonManifoldID = -2;
	/** InvalidGroupID indicates that a group ID is invalid */
	constexpr static int InvalidGroupID = IndexConstants::InvalidID;

	constexpr static FVector3d InvalidVertex{ TNumericLimits<double>::Max(), 0.0, 0.0 };
	constexpr static FIndex3i  InvalidTriangle{ InvalidID, InvalidID, InvalidID };
	constexpr static FIndex2i  InvalidEdge{ InvalidID, InvalidID };

protected:
	/** List of vertex positions */
	TDynamicVector<double> Vertices{};
	/** Reference counts of vertex indices. For vertices that exist, the count is 1 + num_triangle_using_vertex. Iterate over this to find out which vertex indices are valid. */
	FRefCountVector VertexRefCounts{};
	/** (optional) List of per-vertex normals */
	TOptional<TDynamicVector<float>> VertexNormals{};
	/** (optional) List of per-vertex colors */
	TOptional<TDynamicVector<float>> VertexColors{};
	/** (optional) List of per-vertex uv's */
	TOptional<TDynamicVector<float>> VertexUVs{};
	/** List of per-vertex edge one-rings */
	FSmallListSet VertexEdgeLists;

	/** List of triangle vertex-index triplets [Vert0 Vert1 Vert2]*/
	TDynamicVector<int> Triangles;
	/** Reference counts of triangle indices. Ref count is always 1 if the triangle exists. Iterate over this to find out which triangle indices are valid. */
	FRefCountVector TriangleRefCounts;
	/** List of triangle edge triplets [Edge0 Edge1 Edge2] */
	TDynamicVector<int> TriangleEdges;
	/** (optional) List of per-triangle group identifiers */
	TOptional<TDynamicVector<int>> TriangleGroups{};
	/** Upper bound on the triangle group IDs used in the mesh (may be larger than the actual maximum if triangles have been deleted) */
	int GroupIDCounter = 0;

	/** List of edge elements. An edge is four elements [VertA, VertB, Tri0, Tri1], where VertA < VertB, and Tri1 may be InvalidID (if the edge is a boundary edge) */
	TDynamicVector<int> Edges;
	/** Reference counts of edge indices. Ref count is always 1 if the edge exists. Iterate over this to find out which edge indices are valid. */
	FRefCountVector EdgeRefCounts;

	TUniquePtr<FDynamicMeshAttributeSet> AttributeSet{};

	/** The mesh timestamp is incremented any time a function that modifies the mesh is called */
	int Timestamp = 0;
	/** The shape timestamp is incremented any time a function that modifies the mesh shape or topology is called */
	int ShapeTimestamp = 0;
	/** The topology timestamp is incremented any time a function that modifies the mesh topology is called */
	int TopologyTimestamp = 0;


	/** Cached vertex bounding box (includes unreferenced vertices) */
	FAxisAlignedBox3d CachedBoundingBox;
	/** timestamp for CachedBoundingBox, if less than current timestamp, cache is invalid */
	int CachedBoundingBoxTimestamp = -1;
	/** Cached value of IsClosed() */
	bool bIsClosedCached = false;
	/** timestamp for bIsClosedCached, if less than current timestamp, cache is invalid */
	int CachedIsClosedTimestamp = -1;

public:
	/** Default constructor */
	FDynamicMesh3() 
		: FDynamicMesh3(true, false, false, false)
	{
	}

	/** Copy/Move construction */
	FDynamicMesh3(const FDynamicMesh3& CopyMesh );
	FDynamicMesh3(FDynamicMesh3&& MoveMesh);

	/** Copy and move assignment */
	const FDynamicMesh3& operator=(const FDynamicMesh3& CopyMesh);
	const FDynamicMesh3& operator=(FDynamicMesh3&& MoveMesh);

	/** Destructor */
	virtual ~FDynamicMesh3();


	/** Construct an empty mesh with specified attributes */
	explicit FDynamicMesh3(bool bWantNormals, bool bWantColors, bool bWantUVs, bool bWantTriGroups);
	explicit FDynamicMesh3(EMeshComponents flags)
		: FDynamicMesh3(((int)flags & (int)EMeshComponents::VertexNormals) != 0, ((int)flags & (int)EMeshComponents::VertexColors) != 0,
						((int)flags & (int)EMeshComponents::VertexUVs) != 0, ((int)flags & (int)EMeshComponents::FaceGroups) != 0)
	{
	}

	/** Construction from Mesh Generator */
	FDynamicMesh3(const FMeshShapeGenerator* Generator);

	/** Set internal data structures to be a copy of input mesh using the specified attributes*/
	void Copy(const FDynamicMesh3& CopyMesh, bool bNormals = true, bool bColors = true, bool bUVs = true, bool bAttributes = true);

	/** Initialize mesh from the output of a MeshShapeGenerator (assumes Generate() was already called) */
	void Copy(const FMeshShapeGenerator* Generator);

	/** Copy input mesh while compacting, i.e. removing unused vertices/triangles/edges */
	void CompactCopy(const FDynamicMesh3& CopyMesh, bool bNormals = true, bool bColors = true, bool bUVs = true, bool bAttributes = true, FCompactMaps* CompactInfo = nullptr);

	/** Discard all data */
	void Clear();

	
public:

	/** @return number of vertices in the mesh */
	int VertexCount() const
	{
		return (int)VertexRefCounts.GetCount();
	}
	/** @return number of triangles in the mesh */
	int TriangleCount() const
	{
		return (int)TriangleRefCounts.GetCount();
	}
	/** @return number of edges in the mesh */
	int EdgeCount() const
	{
		return (int)EdgeRefCounts.GetCount();
	}

	/** @return upper bound on vertex IDs used in the mesh, i.e. all vertex IDs in use are < MaxVertexID */
	int MaxVertexID() const
	{
		return (int)VertexRefCounts.GetMaxIndex();
	}
	/** @return upper bound on triangle IDs used in the mesh, i.e. all triangle IDs in use are < MaxTriangleID */
	int MaxTriangleID() const
	{
		return (int)TriangleRefCounts.GetMaxIndex();
	}
	/** @return upper bound on edge IDs used in the mesh, i.e. all edge IDs in use are < MaxEdgeID */
	int MaxEdgeID() const
	{
		return (int)EdgeRefCounts.GetMaxIndex();
	}
	/** @return upper bound on group IDs used in the mesh, i.e. all group IDs in use are < MaxGroupID */
	int MaxGroupID() const
	{
		return GroupIDCounter;
	}


	/** @return true if this mesh has per-vertex normals */
	bool HasVertexNormals() const
	{
		return VertexNormals.IsSet();
	}
	/** @return true if this mesh has per-vertex colors */
	bool HasVertexColors() const
	{
		return VertexColors.IsSet();
	}
	/** @return true if this mesh has per-vertex UVs */
	bool HasVertexUVs() const
	{
		return VertexUVs.IsSet();
	}
	/** @return true if this mesh has per-triangle groups */
	bool HasTriangleGroups() const
	{
		return TriangleGroups.IsSet();
	}
	/** @return true if this mesh has attribute layers */
	bool HasAttributes() const
	{
		return AttributeSet.IsValid();
	}

	/** @return bitwise-or of EMeshComponents flags specifying which extra data this mesh has */
	int GetComponentsFlags() const;


	/** @return true if VertexID is a valid vertex in this mesh */
	inline bool IsVertex(int VertexID) const
	{
		return VertexRefCounts.IsValid(VertexID);
	}
	/** @return true if VertexID is a valid vertex in this mesh AND is used by at least one triangle */
	inline bool IsReferencedVertex(int VertexID) const
	{
		return VertexID >= 0 && VertexID < (int)VertexRefCounts.GetMaxIndex() && VertexRefCounts.GetRawRefCount(VertexID) > 1;
	}
	/** @return true if TriangleID is a valid triangle in this mesh */
	inline bool IsTriangle(int TriangleID) const
	{
		return TriangleRefCounts.IsValid(TriangleID);
	}
	/** @return true if EdgeID is a valid edge in this mesh */
	inline bool IsEdge(int EdgeID) const
	{
		return EdgeRefCounts.IsValid(EdgeID);
	}


	//
	// Mesh Element Iterators
	//   The functions VertexIndicesItr() / TriangleIndicesItr() / EdgeIndicesItr() allow you to do:
	//      for ( int eid : EdgeIndicesItr() ) { ... }
	//   and other related begin() / end() idioms
public:
	// simplify names for iterations
	typedef typename FRefCountVector::IndexEnumerable vertex_iterator;
	typedef typename FRefCountVector::IndexEnumerable triangle_iterator;
	typedef typename FRefCountVector::IndexEnumerable edge_iterator;
	template <typename T>
	using value_iteration = FRefCountVector::MappedEnumerable<T>;
	using vtx_triangles_enumerable = TPairExpandEnumerable<FSmallListSet::ValueIterator>;

	/** @return enumerable object for valid vertex indices suitable for use with range-based for, ie for ( int i : VertexIndicesItr() ) */
	vertex_iterator VertexIndicesItr() const
	{
		return VertexRefCounts.Indices();
	}

	/** @return enumerable object for valid triangle indices suitable for use with range-based for, ie for ( int i : TriangleIndicesItr() ) */
	triangle_iterator TriangleIndicesItr() const
	{
		return TriangleRefCounts.Indices();
	}

	/** @return enumerable object for valid edge indices suitable for use with range-based for, ie for ( int i : EdgeIndicesItr() ) */
	edge_iterator EdgeIndicesItr() const
	{
		return EdgeRefCounts.Indices();
	}

	// TODO: write helper functions that allow us to do these iterations w/o lambdas

	/** @return enumerable object for boundary edge indices suitable for use with range-based for, ie for ( int i : BoundaryEdgeIndicesItr() ) */
	FRefCountVector::FilteredEnumerable BoundaryEdgeIndicesItr() const
	{
		return EdgeRefCounts.FilteredIndices([this](int EdgeID) {\
			return Edges[4*EdgeID + 3] == InvalidID;
		});
	}

	/** Enumerate positions of all vertices in mesh */
	value_iteration<FVector3d> VerticesItr() const
	{
		return VertexRefCounts.MappedIndices<FVector3d>([this](int VertexID) {
			int i = 3 * VertexID;
			return FVector3d(Vertices[i], Vertices[i + 1], Vertices[i + 2]);
		});
	}

	/** Enumerate all triangles in the mesh */
	value_iteration<FIndex3i> TrianglesItr() const
	{
		return TriangleRefCounts.MappedIndices<FIndex3i>([this](int TriangleID) {
			int i = 3 * TriangleID;
			return FIndex3i(Triangles[i], Triangles[i + 1], Triangles[i + 2]);
		});
	}

	/** Enumerate edges. Each returned element is [v0,v1,t0,t1], where t1 will be InvalidID if this is a boundary edge */
	value_iteration<FIndex4i> EdgesItr() const
	{
		return EdgeRefCounts.MappedIndices<FIndex4i>([this](int EdgeID) {
			int i = 4 * EdgeID;
			return FIndex4i(Edges[i], Edges[i + 1], Edges[i + 2], Edges[i + 3]);
		});
	}

	/** @return enumerable object for one-ring vertex neighbours of a vertex, suitable for use with range-based for, ie for ( int i : VtxVerticesItr(VertexID) ) */
	FSmallListSet::ValueEnumerable VtxVerticesItr(int VertexID) const
	{
		check(VertexRefCounts.IsValid(VertexID));
		return VertexEdgeLists.Values(VertexID, [VertexID, this](int eid) { return GetOtherEdgeVertex(eid, VertexID); });
	}

	/** @return enumerable object for one-ring edges of a vertex, suitable for use with range-based for, ie for ( int i : VtxEdgesItr(VertexID) ) */
	FSmallListSet::ValueEnumerable VtxEdgesItr(int VertexID) const
	{
		check(VertexRefCounts.IsValid(VertexID));
		return VertexEdgeLists.Values(VertexID);
	}

	/** @return enumerable object for one-ring triangles of a vertex, suitable for use with range-based for, ie for ( int i : VtxTrianglesItr(VertexID) ) */
	vtx_triangles_enumerable VtxTrianglesItr(int VertexID) const
	{
		check(VertexRefCounts.IsValid(VertexID));
		return vtx_triangles_enumerable(VertexEdgeLists.Values(VertexID), [this, VertexID](int EdgeID) {
			return GetOrderedOneRingEdgeTris(VertexID, EdgeID);
		});
	}

	//
	// Mesh Construction
	//
public:
	/** Append vertex at position and other fields, returns vid */
	int AppendVertex(const FVertexInfo& VertInfo);

	/** Append vertex at position, returns vid */
	int AppendVertex(const FVector3d& Position)
	{
		return AppendVertex(FVertexInfo(Position));
	}

	/** Copy vertex SourceVertexID from existing SourceMesh, returns new vertex id */
	int AppendVertex(const FDynamicMesh3& SourceMesh, int SourceVertexID);

	int AppendTriangle(const FIndex3i& TriVertices, int GroupID = -1);

	inline int AppendTriangle(int Vertex0, int Vertex1, int Vertex2, int GroupID = -1)
	{
		return AppendTriangle(FIndex3i(Vertex0, Vertex1, Vertex2), GroupID);
	}

	//
	// Support for inserting vertex and triangle at specific IDs. This is a bit tricky
	// because we likely will need to update the free lists in the RefCountVectors, which
	// can be expensive. If you are going to do many inserts (eg inside a loop), wrap in
	// BeginUnsafe / EndUnsafe calls, and pass bUnsafe = true to the InsertX() calls, to
	// the defer free list rebuild until you are done.
	//

	/** Call this before a set of unsafe InsertVertex() calls */
	virtual void BeginUnsafeVerticesInsert()
	{
		// do nothing...
	}

	/** Call after a set of unsafe InsertVertex() calls to rebuild free list */
	virtual void EndUnsafeVerticesInsert()
	{
		VertexRefCounts.RebuildFreeList();
	}

	/**
	 * Insert vertex at given index, assuming it is unused.
	 * If bUnsafe, we use fast id allocation that does not update free list.
	 * You should only be using this between BeginUnsafeVerticesInsert() / EndUnsafeVerticesInsert() calls
	 */
	EMeshResult InsertVertex(int VertexID, const FVertexInfo& VertInfo, bool bUnsafe = false);

	/** Call this before a set of unsafe InsertTriangle() calls */
	virtual void BeginUnsafeTrianglesInsert()
	{
		// do nothing...
	}

	/** Call after a set of unsafe InsertTriangle() calls to rebuild free list */
	virtual void EndUnsafeTrianglesInsert()
	{
		TriangleRefCounts.RebuildFreeList();
	}

	/**
	 * Insert triangle at given index, assuming it is unused.
	 * If bUnsafe, we use fast id allocation that does not update free list.
	 * You should only be using this between BeginUnsafeTrianglesInsert() / EndUnsafeTrianglesInsert() calls
	 */
	EMeshResult InsertTriangle(int TriangleID, const FIndex3i& TriVertices, int GroupID = -1, bool bUnsafe = false);

	//
	// Vertex/Tri/Edge accessors
	//
public:

	/** @return the vertex position */
	inline FVector3d GetVertex(int VertexID) const
	{
		check(IsVertex(VertexID));
		int i = 3 * VertexID;
		return FVector3d(Vertices[i], Vertices[i + 1], Vertices[i + 2]);
	}

	/** Set vertex position */
	inline void SetVertex(int VertexID, const FVector3d& vNewPos)
	{
		checkSlow(VectorUtil::IsFinite(vNewPos));
		check(IsVertex(VertexID));
		if (VectorUtil::IsFinite(vNewPos))
		{
			int i = 3 * VertexID;
			Vertices[i] = vNewPos.X;
			Vertices[i + 1] = vNewPos.Y;
			Vertices[i + 2] = vNewPos.Z;
			UpdateTimeStamp(true, false);
		}
	}

	/** Get extended vertex information */
	bool GetVertex(int VertexID, FVertexInfo& VertInfo, bool bWantNormals, bool bWantColors, bool bWantUVs) const;

	/** Get all vertex information available */
	FVertexInfo GetVertexInfo(int VertexID) const;

	/** @return the valence of a vertex (the number of connected edges) */
	int GetVtxEdgeCount(int VertexID) const
	{
		return VertexRefCounts.IsValid(VertexID) ? VertexEdgeLists.GetCount(VertexID) : -1;
	}

	/** @return the max valence of all vertices in the mesh */
	int GetMaxVtxEdgeCount() const;

	/** adds triangles touching VertexID to the TrianglesOut list */
	void GetVertexOneRingTriangles(int VertexID, TArray<int>& TrianglesOut) const;

	/** Get triangle vertices */
	inline FIndex3i GetTriangle(int TriangleID) const
	{
		check(IsTriangle(TriangleID));
		int i = 3 * TriangleID;
		return FIndex3i(Triangles[i], Triangles[i + 1], Triangles[i + 2]);
	}

	/** Get triangle edges */
	inline FIndex3i GetTriEdges(int TriangleID) const
	{
		check(IsTriangle(TriangleID));
		int i = 3 * TriangleID;
		return FIndex3i(TriangleEdges[i], TriangleEdges[i + 1], TriangleEdges[i + 2]);
	}

	/** Get one of the edges of a triangle */
	inline int GetTriEdge(int TriangleID, int j) const
	{
		check(IsTriangle(TriangleID));
		return TriangleEdges[3 * TriangleID + j];
	}

	/** Find the neighbour triangles of a triangle (any of them might be InvalidID) */
	FIndex3i GetTriNeighbourTris(int TriangleID) const;

	/** Get the three vertex positions of a triangle */
	inline void GetTriVertices(int TriangleID, FVector3d& v0, FVector3d& v1, FVector3d& v2) const
	{
		int i = 3 * TriangleID;
		int ai = 3 * Triangles[i];
		v0.X = Vertices[ai];		v0.Y = Vertices[ai + 1];		v0.Z = Vertices[ai + 2];
		int bi = 3 * Triangles[i + 1];
		v1.X = Vertices[bi];		v1.Y = Vertices[bi + 1];		v1.Z = Vertices[bi + 2];
		int ci = 3 * Triangles[i + 2];
		v2.X = Vertices[ci];		v2.Y = Vertices[ci + 1];		v2.Z = Vertices[ci + 2];
	}

	/** Get the position of one of the vertices of a triangle */
	inline FVector3d GetTriVertex(int TriangleID, int j) const
	{
		int vi = 3 * Triangles[3 * TriangleID + j];
		return FVector3d(Vertices[vi], Vertices[vi + 1], Vertices[vi + 2]);
	}

	/** Get the vertices and triangles of an edge, returned as [v0,v1,t0,t1], where t1 may be InvalidID */
	inline FIndex4i GetEdge(int EdgeID) const
	{
		check(IsEdge(EdgeID));
		int i = 4 * EdgeID;
		return FIndex4i(Edges[i], Edges[i + 1], Edges[i + 2], Edges[i + 3]);
	}

	/** Get the vertex pair for an edge */
	inline FIndex2i GetEdgeV(int EdgeID) const
	{
		check(IsEdge(EdgeID));
		int i = 4 * EdgeID;
		return FIndex2i(Edges[i], Edges[i + 1]);
	}

	/** Get the vertex positions of an edge */
	inline bool GetEdgeV(int EdgeID, FVector3d& a, FVector3d& b) const
	{
		check(IsEdge(EdgeID));
		int iv0 = 3 * Edges[4 * EdgeID];
		a.X = Vertices[iv0];
		a.Y = Vertices[iv0 + 1];
		a.Z = Vertices[iv0 + 2];
		int iv1 = 3 * Edges[4 * EdgeID + 1];
		b.X = Vertices[iv1];
		b.Y = Vertices[iv1 + 1];
		b.Z = Vertices[iv1 + 2];
		return true;
	}

	/** Get the triangle pair for an edge. The second triangle may be InvalidID */
	inline FIndex2i GetEdgeT(int EdgeID) const
	{
		check(IsEdge(EdgeID));
		int i = 4 * EdgeID;
		return FIndex2i(Edges[i + 2], Edges[i + 3]);
	}

	/** Return edge vertex indices, but oriented based on attached triangle (rather than min-sorted) */
	FIndex2i GetOrientedBoundaryEdgeV(int EdgeID) const;

	//
	// Vertex and Triangle attribute arrays
	//
public:
	void EnableVertexNormals(const FVector3f& InitialNormal);
	void DiscardVertexNormals();

	FVector3f GetVertexNormal(int vID) const
	{
		if (HasVertexNormals() == false)
		{
			return FVector3f::UnitY();
		}
		check(IsVertex(vID));
		int i = 3 * vID;
		const TDynamicVector<float>& Normals = VertexNormals.GetValue();
		return FVector3f( Normals[i], Normals[i + 1], Normals[i + 2] );
	}

	void SetVertexNormal(int vID, const FVector3f& vNewNormal)
	{
		if (HasVertexNormals())
		{
			check(IsVertex(vID));
			int i = 3 * vID;
			TDynamicVector<float>& Normals = VertexNormals.GetValue();
			Normals[i]     = vNewNormal.X;
			Normals[i + 1] = vNewNormal.Y;
			Normals[i + 2] = vNewNormal.Z;
			UpdateTimeStamp(false, false);
		}
	}

	void EnableVertexColors(const FVector3f& InitialColor);
	void DiscardVertexColors();


	FVector3f GetVertexColor(int vID) const
	{
		if (HasVertexColors() == false)
		{
			return FVector3f::One();
		}
		check(IsVertex(vID));
		int i = 3 * vID;

		const TDynamicVector<float>& Colors = VertexColors.GetValue();
		return FVector3f(Colors[i], Colors[i + 1], Colors[i + 2]);
	}

	void SetVertexColor(int vID, const FVector3f& vNewColor)
	{
		if (HasVertexColors())
		{
			check(IsVertex(vID));
			int i = 3 * vID;
			TDynamicVector<float>& Colors = VertexColors.GetValue();
			Colors[i] = vNewColor.X;
			Colors[i + 1] = vNewColor.Y;
			Colors[i + 2] = vNewColor.Z;
			UpdateTimeStamp(false, false);
		}
	}

	void EnableVertexUVs(const FVector2f& InitialUV);
	void DiscardVertexUVs();

	FVector2f GetVertexUV(int vID) const
	{
		if (HasVertexUVs() == false)
		{
			return FVector2f::Zero();
		}
		check(IsVertex(vID));
		int i = 2 * vID;
		const TDynamicVector<float> UVs = VertexUVs.GetValue();
		return FVector2f(UVs[i], UVs[i + 1]);
	}

	void SetVertexUV(int vID, const FVector2f& vNewUV)
	{
		if (HasVertexUVs())
		{
			check(IsVertex(vID));
			int i = 2 * vID;
			TDynamicVector<float> UVs = VertexUVs.GetValue();
			UVs[i] = vNewUV.X;
			UVs[i + 1] = vNewUV.Y;
			UpdateTimeStamp(false, false);
		}
	}

	void EnableTriangleGroups(int InitialGroupID = 0);
	void DiscardTriangleGroups();

	int AllocateTriangleGroup()	{ return ++GroupIDCounter; }

	int GetTriangleGroup(int tID) const
	{
		return (HasTriangleGroups() == false) ? -1
			: (TriangleRefCounts.IsValid(tID) ? TriangleGroups.GetValue()[tID] : 0);
	}

	void SetTriangleGroup(int tid, int group_id)
	{
		if (HasTriangleGroups())
		{
			check(IsTriangle(tid));
			TriangleGroups.GetValue()[tid] = group_id;
			GroupIDCounter = FMath::Max(GroupIDCounter, group_id + 1);
			UpdateTimeStamp(false, false);
		}
	}

	FDynamicMeshAttributeSet* Attributes()
	{
		return HasAttributes() ? AttributeSet.Get() : nullptr;
	}
	const FDynamicMeshAttributeSet* Attributes() const
	{
		return HasAttributes() ? AttributeSet.Get() : nullptr;
	}

	void EnableAttributes();
	void DiscardAttributes();


	//
	// topological queries
	//
public:
	/** Returns true if edge is on the mesh boundary, ie only connected to one triangle */
	inline bool IsBoundaryEdge(int EdgeID) const
	{
		check(IsEdge(EdgeID));
		return Edges[4 * EdgeID + 3] == InvalidID;
	}

	/** Returns true if the vertex is part of any boundary edges */
	bool IsBoundaryVertex(int VertexID) const;

	/** Returns true if any edge of triangle is a boundary edge */
	bool IsBoundaryTriangle(int TriangleID) const;

	/** Find id of edge connecting A and B */
	int FindEdge(int VertexA, int VertexB) const;

	/** Find edgeid for edge [a,b] from triangle that contains the edge. Faster than FindEdge() because it is constant-time. */
	int FindEdgeFromTri(int VertexA, int VertexB, int TriangleID) const;

	/** Find edgeid for edge connecting two triangles */
	int FindEdgeFromTriPair(int TriangleA, int TriangleB) const;

	/** Find triangle made up of any permutation of vertices [a,b,c] */
	int FindTriangle(int A, int B, int C) const;

	/**
	 * If edge has vertices [a,b], and is connected two triangles [a,b,c] and [a,b,d],
	 * this returns [c,d], or [c,InvalidID] for a boundary edge
	 */
	FIndex2i GetEdgeOpposingV(int EdgeID) const;

	/**
	 * Given an edge and vertex on that edge, returns other vertex of edge, the two opposing verts, and the two connected triangles (OppVert2Out and Tri2Out are be InvalidID for boundary edge)
	 */
	void GetVtxNbrhood(int EdgeID, int VertexID, int& OtherVertOut, int& OppVert1Out, int& OppVert2Out, int& Tri1Out, int& Tri2Out) const;

	/**
	 * Returns count of boundary edges at vertex, and the first two boundary
	 * edges if found. If return is > 2, call GetAllVtxBoundaryEdges
	 */
	int GetVtxBoundaryEdges(int VertexID, int& Edge0Out, int& Edge1Out) const;

	/**
	 * Find edge ids of boundary edges connected to vertex.
	 * @param vID Vertex ID
	 * @param EdgeListOut boundary edge IDs are appended to this list
	 * @return count of number of elements of e that were filled
	 */
	int GetAllVtxBoundaryEdges(int VertexID, TArray<int>& EdgeListOut) const;

	/**
	 * return # of triangles attached to vID, or -1 if invalid vertex
	 * if bBruteForce = true, explicitly checks, which creates a list and is expensive
	 * default is false, uses orientation, no memory allocation
	 */
	int GetVtxTriangleCount(int VertexID, bool bBruteForce = false) const;

	/**
	 * Get triangle one-ring at vertex.
	 * bUseOrientation is more efficient but returns incorrect result if vertex is a bowtie
	 */
	EMeshResult GetVtxTriangles(int VertexID, TArray<int>& TrianglesOut, bool bUseOrientation) const;

	/**
	* Get triangles connected to vertex in contiguous order, with multiple groups if vertex is a bowtie.
	* @param VertexID Vertex ID to search around
	* @param TrianglesOut All triangles connected to the vertex, in contiguous order; if there are multiple contiguous groups they are packed one after another
	* @param ContiguousGroupLengths Lengths of contiguous groups packed into TrianglesOut (if not a bowtie, this will just be a length-one array w/ {TrianglesOut.Num()})
	* @param GroupIsLoop Indicates whether each contiguous group is a loop (first triangle connected to last) or not
	*/
	EMeshResult GetVtxContiguousTriangles(int VertexID, TArray<int>& TrianglesOut, TArray<int>& ContiguousGroupLengths, TArray<bool>& GroupIsLoop) const;

	/** Returns true if the two triangles connected to edge have different group IDs */
	bool IsGroupBoundaryEdge(int EdgeID) const;

	/** Returns true if vertex has more than one tri group in its tri nbrhood */
	bool IsGroupBoundaryVertex(int VertexID) const;

	/** Returns true if more than two group boundary edges meet at vertex (ie 3+ groups meet at this vertex) */
	bool IsGroupJunctionVertex(int VertexID) const;

	/** Returns up to 4 group IDs at vertex. Returns false if > 4 encountered */
	bool GetVertexGroups(int VertexID, FIndex4i& GroupsOut) const;

	/** Returns all group IDs at vertex */
	bool GetAllVertexGroups(int VertexID, TArray<int>& GroupsOut) const;

	/** returns true if vID is a "bowtie" vertex, ie multiple disjoint triangle sets in one-ring */
	bool IsBowtieVertex(int VertexID) const;

	/** returns true if vertices, edges, and triangles are all dense (Count == MaxID) **/
	bool IsCompact() const
	{
		return VertexRefCounts.IsDense() && EdgeRefCounts.IsDense() && TriangleRefCounts.IsDense();
	}

	/** @return true if vertex count == max vertex id */
	bool IsCompactV() const
	{
		return VertexRefCounts.IsDense();
	}

	/** @return true if triangle count == max triangle id */
	bool IsCompactT() const
	{
		return TriangleRefCounts.IsDense();
	}

	/** returns measure of compactness in range [0,1], where 1 is fully compacted */
	double CompactMetric() const
	{
		return ((double)VertexCount() / (double)MaxVertexID() + (double)TriangleCount() / (double)MaxTriangleID()) * 0.5;
	}

	/** @return true if mesh has no boundary edges */
	bool IsClosed() const;

	/** @return value of IsClosed() and cache result. cache is invalidated and recomputed if topology has changed since last call */
	bool GetCachedIsClosed();


	/** Timestamp is incremented any time any change is made to the mesh */
	inline int GetTimestamp() const
	{
		return Timestamp;
	}

	/** ShapeTimestamp is incremented any time any vertex position is changed or the mesh topology is modified */
	inline int GetShapeTimestamp() const
	{
		return ShapeTimestamp;
	}

	/** TopologyTimestamp is incremented any time any vertex position is changed or the mesh topology is modified */
	inline int GetTopologyTimestamp() const
	{
		return TopologyTimestamp;
	}

	//
	// Geometric queries
	//
public:
	/** Returns bounding box of all mesh vertices (including unreferenced vertices) */
	FAxisAlignedBox3d GetBounds() const;

	/** Returns GetBounds() and saves result, cache is invalidated and recomputed if topology has changed since last call */
	FAxisAlignedBox3d GetCachedBounds();

	/**
	 * Compute a normal/tangent frame at vertex that is "stable" as long as
	 * the mesh topology doesn't change, meaning that one axis of the frame
	 * will be computed from projection of outgoing edge. Requires that vertex normals are available.
	 * By default, frame.Z is normal, and .X points along mesh edge.
	 * If bFrameNormalY, then frame.Y is normal (X still points along mesh edge)
	 */
	FFrame3d GetVertexFrame(int VertexID, bool bFrameNormalY = false) const;

	/** Calculate face normal of triangle */
	FVector3d GetTriNormal(int TriangleID) const;

	/** Calculate area triangle */
	double GetTriArea(int TriangleID) const;

	/**
	 * Compute triangle normal, area, and centroid all at once. Re-uses vertex
	 * lookups and computes normal & area simultaneously. *However* does not produce
	 * the same normal/area as separate calls, because of this.
	 */
	void GetTriInfo(int TriangleID, FVector3d& Normal, double& Area, FVector3d& Centroid) const;

	/** Compute centroid of triangle */
	FVector3d GetTriCentroid(int TriangleID) const;

	/** Interpolate vertex positions of triangle using barycentric coordinates */
	FVector3d GetTriBaryPoint(int TriangleID, double Bary0, double Bary1, double Bary2) const;

	/** Interpolate vertex normals of triangle using barycentric coordinates */
	FVector3d GetTriBaryNormal(int TriangleID, double Bary0, double Bary1, double Bary2) const;

	/** Compute interpolated vertex attributes at point of triangle */
	void GetTriBaryPoint(int TriangleID, double Bary0, double Bary1, double Bary2, FVertexInfo& VertInfo) const;

	/** Construct bounding box of triangle as efficiently as possible */
	FAxisAlignedBox3d GetTriBounds(int TriangleID) const;

	/** Construct stable frame at triangle centroid, where frame.Z is face normal, and frame.X is aligned with edge nEdge of triangle. */
	FFrame3d GetTriFrame(int TriangleID, int Edge = 0) const;

	/** Compute solid angle of oriented triangle tID relative to point p - see WindingNumber() */
	double GetTriSolidAngle(int TriangleID, const FVector3d& p) const;

	/** Compute internal angle at vertex i of triangle (where i is 0,1,2); */
	double GetTriInternalAngleR(int TriangleID, int i) const;

	/** Compute internal angles at all vertices of triangle */
	FVector3d GetTriInternalAnglesR(int TriangleID) const;

	/** Returns average normal of connected face normals */
	FVector3d GetEdgeNormal(int EdgeID) const;

	/** Get point along edge, t clamped to range [0,1] */
	FVector3d GetEdgePoint(int EdgeID, double ParameterT) const;

	/**
	 * Fastest possible one-ring centroid. This is used inside many other algorithms
	 * so it helps to have it be maximally efficient
	 */
	void GetVtxOneRingCentroid(int VertexID, FVector3d& CentroidOut) const;

	/**
	 * Compute mesh winding number, from Jacobson et. al., Robust Inside-Outside Segmentation using Generalized Winding Numbers
	 * http://igl.ethz.ch/projects/winding-number/
	 * returns ~0 for points outside a closed, consistently oriented mesh, and a positive or negative integer
	 * for points inside, with value > 1 depending on how many "times" the point inside the mesh (like in 2D polygon winding)
	 */
	double CalculateWindingNumber(const FVector3d& QueryPoint) const;

	//
	// direct buffer access
	//
public:
	const TDynamicVector<double>& GetVerticesBuffer()
	{
		return Vertices;
	}
	const FRefCountVector& GetVerticesRefCounts()
	{
		return VertexRefCounts;
	}
	const TDynamicVector<float>* GetNormalsBuffer()
	{
		return HasVertexNormals() ? &VertexNormals.GetValue() : nullptr;
	}
	const TDynamicVector<float>* GetColorsBuffer()
	{
		return HasVertexColors() ? &VertexColors.GetValue() : nullptr;
	}
	const TDynamicVector<float>* GetUVBuffer()
	{
		return HasVertexUVs() ? &VertexUVs.GetValue() : nullptr;
	}
	const TDynamicVector<int>& GetTrianglesBuffer()
	{
		return Triangles;
	}
	const FRefCountVector& GetTrianglesRefCounts()
	{
		return TriangleRefCounts;
	}
	const TDynamicVector<int>* GetTriangleGroupsBuffer()
	{
		return HasTriangleGroups() ? &TriangleGroups.GetValue() : nullptr;
	}
	const TDynamicVector<int>& GetEdgesBuffer()
	{
		return Edges;
	}
	const FRefCountVector& GetEdgesRefCounts()
	{
		return EdgeRefCounts;
	}
	const FSmallListSet& GetVertexEdges()
	{
		return VertexEdgeLists;
	}

	//
	// Mesh Edit operations
	//
public:
	/**
	 * Compact mesh in-place, by moving vertices around and rewriting indices.
	 * Should be faster if the amount of compacting is not too significant, and is useful in some places.
	 *
	 * @param CompactInfo if not nullptr, will be filled with mapping indicating how vertex and (optionally) triangle IDs were changed during compaction
	 * @todo VertexEdgeLists is not compacted. does not affect indices, but does keep memory.
	 */
	void CompactInPlace(FCompactMaps* CompactInfo = nullptr);

	/**
	 * Reverse the ccw/cw orientation of all triangles in the mesh, and
	 * optionally flip the vertex normals if they exist
	 */
	void ReverseOrientation(bool bFlipNormals = true);

	/**
	 * Reverse the ccw/cw orientation of a triangle
	 */
	EMeshResult ReverseTriOrientation(int TriangleID);

	/**
	 * Remove vertex vID, and all connected triangles if bRemoveAllTriangles = true
	 * Returns Failed_VertexStillReferenced if vertex is still referenced by triangles.
	 * if bPreserveManifold, checks that we will not create a bow tie vertex first
	 */
	EMeshResult RemoveVertex(int VertexID, bool bRemoveAllTriangles = true, bool bPreserveManifold = false);

	/**
	* Remove a triangle from the mesh. Also removes any unreferenced edges after tri is removed.
	* If bRemoveIsolatedVertices is false, then if you remove all tris from a vert, that vert is also removed.
	* If bPreserveManifold, we check that you will not create a bow tie vertex (and return false).
	* If this check is not done, you have to make sure you don't create a bow tie, because other
	* code assumes we don't have bow ties, and will not handle it properly
	*/
	EMeshResult RemoveTriangle(int TriangleID, bool bRemoveIsolatedVertices = true, bool bPreserveManifold = false);

	/**
	 * Rewrite the triangle to reference the new tuple of vertices.
	 *
	 * @todo this function currently does not guarantee that the returned mesh is well-formed. Only call if you know it's OK.
	 */
	virtual EMeshResult SetTriangle(int TriangleID, const FIndex3i& NewVertices, bool bRemoveIsolatedVertices = true);

	/**
	 * Split an edge of the mesh by inserting a vertex. This creates a new triangle on either side of the edge (ie a 2-4 split).
	 * If the original edge had vertices [a,b], with triangles t0=[a,b,c] and t1=[b,a,d],  then the split inserts new vertex f.
	 * After the split t0=[a,f,c] and t1=[f,a,d], and we have t2=[f,b,c] and t3=[f,d,b]  (it's best to draw it out on paper...)
	 * 
	 * @param EdgeAB index of the edge to be split
	 * @param SplitInfo returned information about new and modified mesh elements
	 * @param SplitParameterT defines the position along the edge that we split at, must be between 0 and 1, and is assumed to be based on the order of vertices returned by GetEdgeV()
	 * @return Ok on success, or enum value indicates why operation cannot be applied. Mesh remains unmodified on error.
	 */
	virtual EMeshResult SplitEdge(int EdgeAB, FEdgeSplitInfo& SplitInfo, double SplitParameterT = 0.5);

	/** 
	 * Splits the edge between two vertices at the midpoint, if this edge exists 
	 * @param EdgeVertA index of first vertex
	 * @param EdgeVertB index of second vertex
	 * @param SplitInfo returned information about new and modified mesh elements
	 * @return Ok on success, or enum value indicates why operation cannot be applied. Mesh remains unmodified on error.
	 */
	EMeshResult SplitEdge(int EdgeVertA, int EdgeVertB, FEdgeSplitInfo& SplitInfo);

	/**
	 * Flip/Rotate an edge of the mesh. This does not change the number of edges, vertices, or triangles.
	 * Boundary edges of the mesh cannot be flipped.
	 * @param EdgeAB index of edge to be flipped
	 * @param FlipInfo returned information about new and modified mesh elements
	 * @return Ok on success, or enum value indicates why operation cannot be applied. Mesh remains unmodified on error.
	 */
	virtual EMeshResult FlipEdge(int EdgeAB, FEdgeFlipInfo& FlipInfo);

	/** calls FlipEdge() on the edge between two vertices, if it exists
	 * @param EdgeVertA index of first vertex
	 * @param EdgeVertB index of second vertex
	 * @param FlipInfo returned information about new and modified mesh elements
	 * @return Ok on success, or enum value indicates why operation cannot be applied. Mesh remains unmodified on error.
	 */
	virtual EMeshResult FlipEdge(int EdgeVertA, int EdgeVertB, FEdgeFlipInfo& FlipInfo);


	/**
	 * Clones the given vertex and updates any provided triangles to use the new vertex if/where they used the old one.
	 * @param VertexID the vertex to split
	 * @param TrianglesToUpdate triangles that should be updated to use the new vertex anywhere they previously had the old one
	 * @param SplitInfo returned info about the new and modified mesh elements
	 * @return Ok on success, or enum value indicates why operation cannot be applied. Mesh remains unmodified on error.
	 */
	virtual EMeshResult SplitVertex(int VertexID, const TArrayView<const int>& TrianglesToUpdate, FVertexSplitInfo& SplitInfo);

	/**
	 * Tests whether splitting the given vertex with the given triangles would leave no triangles attached to the original vertex (creating an isolated vertex)
	 * @param VertexID the vertex to split
	 * @param TrianglesToUpdate triangles that should be updated to use the new vertex anywhere they previously had the old one
	 * @return true if calling SplitVertex with these arguments would leave an isolated vertex at the original VertexID
	 */
	virtual bool SplitVertexWouldLeaveIsolated(int VertexID, const TArrayView<const int>& TrianglesToUpdate);



	/**
	 * Collapse the edge between the two vertices, if topologically possible.
	 * @param KeepVertID index of the vertex that should be kept
	 * @param RemoveVertID index of the vertex that should be removed
	 * @param EdgeParameterT vKeep is moved to Lerp(KeepPos, RemovePos, collapse_t)
	 * @param CollapseInfo returned information about new and modified mesh elements
	 * @return Ok on success, or enum value indicates why operation cannot be applied. Mesh remains unmodified on error.
	 */
	virtual EMeshResult CollapseEdge(int KeepVertID, int RemoveVertID, double EdgeParameterT, FEdgeCollapseInfo& CollapseInfo);
	virtual EMeshResult CollapseEdge(int KeepVertID, int RemoveVertID, FEdgeCollapseInfo& CollapseInfo)
	{
		return CollapseEdge(KeepVertID, RemoveVertID, 0, CollapseInfo);
	}



	/**
	 * Given two edges of the mesh, weld both their vertices, so that one edge is removed.
	 * This could result in one neighbour edge-pair attached to each vertex also collapsing,
	 * so those cases are detected and handled (eg middle edge-pair in abysmal ascii drawing below)
	 *
	 *   ._._._.    (dots are vertices)
	 *    \._./
	 *
	 * @param KeepEdgeID index of the edge that should be kept
	 * @param DiscardEdgeID index of the edge that should be removed
	 * @param MergeInfo returned information about new and modified mesh elements
	 * @return Ok on success, or enum value indicates why operation cannot be applied. Mesh remains unmodified on error.
	 */
	virtual EMeshResult MergeEdges(int KeepEdgeID, int DiscardEdgeID, FMergeEdgesInfo& MergeInfo);



	/**
	 * Insert a new vertex inside a triangle, ie do a 1 to 3 triangle split
	 * @param TriangleID index of triangle to poke
	 * @param BaryCoordinates barycentric coordinates of poke position
	 * @param PokeInfo returned information about new and modified mesh elements
	 * @return Ok on success, or enum value indicates why operation cannot be applied. Mesh remains unmodified on error.
	 */
	virtual EMeshResult PokeTriangle(int TriangleID, const FVector3d& BaryCoordinates, FPokeTriangleInfo& PokeInfo);

	/** Call PokeTriangle at the centroid of the triangle */
	virtual EMeshResult PokeTriangle(int TriangleID, FPokeTriangleInfo& PokeInfo)
	{
		return PokeTriangle(TriangleID, FVector3d::One() / 3.0, PokeInfo);
	}

	//
	// Debug utility functions
	//
public:
	/**
	 * Returns a debug string that contains mesh statistics and other information
	 */
	virtual FString MeshInfoString();

	/**
	 * Check if another mesh is the same as this mesh. By default only checks
	 * vertices and triangles, turn on other parameters w/ flags
	 */
	virtual bool IsSameMesh(const FDynamicMesh3& OtherMesh, bool bCheckConnectivity, bool bCheckEdgeIDs = false,
							bool bCheckNormals = false, bool bCheckColors = false, bool bCheckUVs = false,
							bool bCheckGroups = false,
							float Epsilon = TMathUtil<float>::Epsilon);


	/**
	 * Checks that the mesh is well-formed, ie all internal data structures are consistent
	 */
	virtual bool CheckValidity(bool bAllowNonManifoldVertices = false, EValidityCheckFailMode FailMode = EValidityCheckFailMode::Check) const;

	//
	// Internal functions
	//
protected:

	inline void SetTriangleInternal(int TriangleID, int v0, int v1, int v2)
	{
		int i = 3 * TriangleID;
		Triangles[i] = v0;
		Triangles[i + 1] = v1;
		Triangles[i + 2] = v2;
	}
	inline void SetTriangleEdgesInternal(int TriangleID, int e0, int e1, int e2)
	{
		int i = 3 * TriangleID;
		TriangleEdges[i] = e0;
		TriangleEdges[i + 1] = e1;
		TriangleEdges[i + 2] = e2;
	}

	int AddEdgeInternal(int vA, int vB, int tA, int tB = InvalidID);
	int AddTriangleInternal(int a, int b, int c, int e0, int e1, int e2);

	inline int ReplaceTriangleVertex(int TriangleID, int vOld, int vNew)
	{
		int i = 3 * TriangleID;
		if (Triangles[i] == vOld)
		{
			Triangles[i] = vNew;
			return 0;
		}
		if (Triangles[i + 1] == vOld)
		{
			Triangles[i + 1] = vNew;
			return 1;
		}
		if (Triangles[i + 2] == vOld)
		{
			Triangles[i + 2] = vNew;
			return 2;
		}
		return -1;
	}

	inline void AllocateEdgesList(int VertexID)
	{
		if (VertexID < (int)VertexEdgeLists.Size())
		{
			VertexEdgeLists.Clear(VertexID);
		}
		VertexEdgeLists.AllocateAt(VertexID);
	}

	void GetVertexEdgesList(int VertexID, TArray<int>& EdgesOut) const
	{
		for (int eid : VertexEdgeLists.Values(VertexID))
		{
			EdgesOut.Add(eid);
		}
	}

	inline void SetEdgeVerticesInternal(int EdgeID, int a, int b)
	{
		int i = 4 * EdgeID;
		if (a < b)
		{
			Edges[i] = a;
			Edges[i + 1] = b;
		}
		else
		{
			Edges[i] = b;
			Edges[i + 1] = a;
		}
	}

	inline void SetEdgeTrianglesInternal(int EdgeID, int t0, int t1)
	{
		int i = 4 * EdgeID;
		Edges[i + 2] = t0;
		Edges[i + 3] = t1;
	}

	int ReplaceEdgeVertex(int EdgeID, int vOld, int vNew);
	int ReplaceEdgeTriangle(int EdgeID, int tOld, int tNew);
	int ReplaceTriangleEdge(int EdgeID, int eOld, int eNew);

	inline bool TriangleHasVertex(int TriangleID, int VertexID) const
	{
		int i = 3 * TriangleID;
		return Triangles[i] == VertexID || Triangles[i + 1] == VertexID || Triangles[i + 2] == VertexID;
	}

	inline bool TriHasNeighbourTri(int CheckTriID, int NbrTriID) const
	{
		int i = 3 * CheckTriID;
		return EdgeHasTriangle(TriangleEdges[i], NbrTriID) || EdgeHasTriangle(TriangleEdges[i + 1], NbrTriID) || EdgeHasTriangle(TriangleEdges[i + 2], NbrTriID);
	}

	inline bool TriHasSequentialVertices(int TriangleID, int vA, int vB) const
	{
		int i = 3 * TriangleID;
		int v0 = Triangles[i], v1 = Triangles[i + 1], v2 = Triangles[i + 2];
		return ((v0 == vA && v1 == vB) || (v1 == vA && v2 == vB) || (v2 == vA && v0 == vB));
	}

	int FindTriangleEdge(int TriangleID, int vA, int vB) const;

	inline bool EdgeHasVertex(int EdgeID, int VertexID) const
	{
		int i = 4 * EdgeID;
		return (Edges[i] == VertexID) || (Edges[i + 1] == VertexID);
	}
	inline bool EdgeHasTriangle(int EdgeID, int TriangleID) const
	{
		int i = 4 * EdgeID;
		return (Edges[i + 2] == TriangleID) || (Edges[i + 3] == TriangleID);
	}

	inline int GetOtherEdgeVertex(int EdgeID, int VertexID) const
	{
		int i = 4 * EdgeID;
		int ev0 = Edges[i], ev1 = Edges[i + 1];
		return (ev0 == VertexID) ? ev1 : ((ev1 == VertexID) ? ev0 : InvalidID);
	}
	inline int GetOtherEdgeTriangle(int EdgeID, int TriangleID) const
	{
		int i = 4 * EdgeID;
		int et0 = Edges[i + 2], et1 = Edges[i + 3];
		return (et0 == TriangleID) ? et1 : ((et1 == TriangleID) ? et0 : InvalidID);
	}

	inline void AddTriangleEdge(int TriangleID, int v0, int v1, int j, int EdgeID)
	{
		if (EdgeID != InvalidID)
		{
			Edges[4 * EdgeID + 3] = TriangleID;
			TriangleEdges.InsertAt(EdgeID, 3 * TriangleID + j);
		}
		else
		{
			TriangleEdges.InsertAt(AddEdgeInternal(v0, v1, TriangleID), 3 * TriangleID + j);
		}
	}

	// utility function that returns one or two triangles of edge, used to enumerate vertex one-ring triangles
	// The logic is a bit tricky to follow without drawing it out on paper, but this will only return 
	// each triangle once, for the 'outgoing' edge from the vertex, and each triangle only has one such edge
	// at any vertex (including boundary triangles)
	inline FIndex2i GetOrderedOneRingEdgeTris(int VertexID, int EdgeID) const
	{
		int vOther = GetOtherEdgeVertex(EdgeID, VertexID);
		int i = 4 * EdgeID;
		int et1 = Edges[i + 3];
		et1 = (et1 != InvalidID && TriHasSequentialVertices(et1, VertexID, vOther)) ? et1 : InvalidID;
		int et0 = Edges[i + 2];
		return TriHasSequentialVertices(et0, VertexID, vOther) ? 
			FIndex2i(et0, et1) : FIndex2i(et1, InvalidID);
	}


	void ReverseTriOrientationInternal(int TriangleID);

	void UpdateTimeStamp(bool bShapeChange, bool bTopologyChange)
	{
		Timestamp++;
		if (bShapeChange)
		{
			ShapeTimestamp++;
		}
		if (bTopologyChange)
		{
			check(bShapeChange);   // we consider topology change to be a shape change!
			TopologyTimestamp++;
		}
	}



};


