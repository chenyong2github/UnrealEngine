// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "Generators/MeshShapeGenerator.h"
#include "Templates/UniquePtr.h"

// NB: These have to be here until C++17 allows inline variables
constexpr int       FDynamicMesh3::InvalidID;
constexpr int       FDynamicMesh3::NonManifoldID;
constexpr int       FDynamicMesh3::DuplicateTriangleID;
constexpr FVector3d FDynamicMesh3::InvalidVertex;
constexpr FIndex3i  FDynamicMesh3::InvalidTriangle;
constexpr FIndex2i  FDynamicMesh3::InvalidEdge;

FDynamicMesh3::FDynamicMesh3(bool bWantNormals, bool bWantColors, bool bWantUVs, bool bWantTriGroups)
{
	if ( bWantNormals )   { VertexNormals = TDynamicVector<FVector3f>{}; }
	if ( bWantColors )    { VertexColors = TDynamicVector<FVector3f>{}; }
	if ( bWantUVs )       { VertexUVs = TDynamicVector<FVector2f>{}; }
	if ( bWantTriGroups ) { TriangleGroups = TDynamicVector<int>{}; }
}

// normals/colors/uvs will only be copied if they exist
FDynamicMesh3::FDynamicMesh3(const FDynamicMesh3& Other)
	:
	Vertices{ Other.Vertices },
	VertexRefCounts{ Other.VertexRefCounts },
	VertexNormals{ Other.VertexNormals },
	VertexColors{ Other.VertexColors },
	VertexUVs{ Other.VertexUVs },
	VertexEdgeLists{ Other.VertexEdgeLists },

	Triangles{ Other.Triangles },
	TriangleRefCounts{ Other.TriangleRefCounts },
	TriangleEdges{ Other.TriangleEdges },
	TriangleGroups{ Other.TriangleGroups },
	GroupIDCounter{ Other.GroupIDCounter },

	Edges{ Other.Edges },
	EdgeRefCounts{ Other.EdgeRefCounts },

	Timestamp{ Other.Timestamp },
	ShapeTimestamp{ Other.ShapeTimestamp },
	TopologyTimestamp{ Other.TopologyTimestamp },

	CachedBoundingBox{ Other.CachedBoundingBox },
	CachedBoundingBoxTimestamp{ Other.CachedBoundingBoxTimestamp },

	bIsClosedCached{ Other.bIsClosedCached },
	CachedIsClosedTimestamp{ Other.CachedIsClosedTimestamp }
{
	if (Other.HasAttributes())
	{
		EnableAttributes();
		AttributeSet->Copy(*Other.AttributeSet);
	}
}

FDynamicMesh3::FDynamicMesh3(FDynamicMesh3&& Other)
	:
	Vertices{ MoveTemp(Other.Vertices) },
	VertexRefCounts{ MoveTemp(Other.VertexRefCounts) },
	VertexNormals{ MoveTemp(Other.VertexNormals) },
	VertexColors{ MoveTemp(Other.VertexColors) },
	VertexUVs{ MoveTemp( Other.VertexUVs ) },
	VertexEdgeLists{ MoveTemp( Other.VertexEdgeLists ) },

	Triangles{ MoveTemp( Other.Triangles ) },
	TriangleRefCounts{ MoveTemp( Other.TriangleRefCounts ) },
	TriangleEdges{ MoveTemp( Other.TriangleEdges ) },
	TriangleGroups{ MoveTemp( Other.TriangleGroups ) },
	GroupIDCounter{ MoveTemp( Other.GroupIDCounter ) },

	Edges{ MoveTemp( Other.Edges ) },
	EdgeRefCounts{ MoveTemp( Other.EdgeRefCounts ) },

	AttributeSet{ MoveTemp( Other.AttributeSet ) },
	Timestamp{ MoveTemp( Other.Timestamp ) },
	ShapeTimestamp{ MoveTemp( Other.ShapeTimestamp ) },
	TopologyTimestamp{ MoveTemp( Other.TopologyTimestamp ) },

	CachedBoundingBox{ MoveTemp( Other.CachedBoundingBox ) },
	CachedBoundingBoxTimestamp{ MoveTemp( Other.CachedBoundingBoxTimestamp ) },

	bIsClosedCached{ Other.bIsClosedCached },
	CachedIsClosedTimestamp{ MoveTemp( Other.CachedIsClosedTimestamp ) }
{
	if (AttributeSet)
	{
		AttributeSet->Reparent(this);
	}
}

FDynamicMesh3::~FDynamicMesh3() = default;

const FDynamicMesh3& FDynamicMesh3::operator=(const FDynamicMesh3& CopyMesh)
{
	Copy(CopyMesh);
	return *this;
}

const FDynamicMesh3 & FDynamicMesh3::operator=(FDynamicMesh3 && Other)
{
	Vertices = MoveTemp(Other.Vertices);
	VertexRefCounts = MoveTemp(Other.VertexRefCounts);
	VertexNormals = MoveTemp(Other.VertexNormals);
	VertexColors = MoveTemp(Other.VertexColors);
	VertexUVs = MoveTemp(Other.VertexUVs);
	VertexEdgeLists = MoveTemp(Other.VertexEdgeLists);

	Triangles = MoveTemp(Other.Triangles);
	TriangleRefCounts = MoveTemp(Other.TriangleRefCounts);
	TriangleEdges = MoveTemp(Other.TriangleEdges);
	TriangleGroups = MoveTemp(Other.TriangleGroups);
	GroupIDCounter = MoveTemp(Other.GroupIDCounter);

	Edges = MoveTemp(Other.Edges);
	EdgeRefCounts = MoveTemp(Other.EdgeRefCounts);

	AttributeSet = MoveTemp(Other.AttributeSet);
	if (AttributeSet)
	{
		AttributeSet->Reparent(this);
	}
	Timestamp = MoveTemp(Other.Timestamp);
	ShapeTimestamp = MoveTemp(Other.ShapeTimestamp);
	TopologyTimestamp = MoveTemp(Other.TopologyTimestamp);

	CachedBoundingBox = MoveTemp(Other.CachedBoundingBox);
	CachedBoundingBoxTimestamp = MoveTemp(Other.CachedBoundingBoxTimestamp);

	bIsClosedCached = MoveTemp(Other.bIsClosedCached);
	CachedIsClosedTimestamp = MoveTemp( Other.CachedIsClosedTimestamp);
	return *this;
}

FDynamicMesh3::FDynamicMesh3(const FMeshShapeGenerator* Generator)
{
	Copy(Generator);
}

void FDynamicMesh3::Copy(const FMeshShapeGenerator* Generator)
{
	Clear();

	Vertices = TDynamicVector<FVector3d>();
	VertexEdgeLists = FSmallListSet();
	VertexRefCounts = FRefCountVector();
	Triangles = TDynamicVector<FIndex3i>();
	TriangleEdges = TDynamicVector<FIndex3i>();
	TriangleRefCounts = FRefCountVector();
	TriangleGroups = TDynamicVector<int>();
	GroupIDCounter = 0;
	Edges = TDynamicVector<FEdge>();
	EdgeRefCounts = FRefCountVector();

	if (Generator->HasAttributes())
	{
		EnableAttributes();
	}


	int NumVerts = Generator->Vertices.Num();
	for (int i = 0; i < NumVerts; ++i)
	{
		AppendVertex(Generator->Vertices[i]);
	}

	if (Generator->HasAttributes())
	{
		FDynamicMeshUVOverlay* UVOverlay = Attributes()->PrimaryUV();
		FDynamicMeshNormalOverlay* NormalOverlay = Attributes()->PrimaryNormals();
		int NumUVs = Generator->UVs.Num();
		for (int i = 0; i < NumUVs; ++i)
		{
			UVOverlay->AppendElement(Generator->UVs[i]);
		}
		int NumNormals = Generator->Normals.Num();
		for (int i = 0; i < NumNormals; ++i)
		{
			NormalOverlay->AppendElement(Generator->Normals[i]);
		}

		int NumTris = Generator->Triangles.Num();
		for (int i = 0; i < NumTris; ++i)
		{
			int PolyID = Generator->TrianglePolygonIDs.Num() > 0 ? 1 + Generator->TrianglePolygonIDs[i] : 0;
			int tid = AppendTriangle(Generator->Triangles[i], PolyID);
			check(tid == i);
			UVOverlay->SetTriangle(tid, Generator->TriangleUVs[i]);
			NormalOverlay->SetTriangle(tid, Generator->TriangleNormals[i]);
		}
	}
	else if (Generator->TrianglePolygonIDs.Num()) // no attributes, yes polygon ids
	{
		int NumTris = Generator->Triangles.Num();
		for (int i = 0; i < NumTris; ++i)
		{
			int tid = AppendTriangle(Generator->Triangles[i], 1 + Generator->TrianglePolygonIDs[i]);
			check(tid == i);
		}
	}
	else // no attribute and no polygon ids
	{
		int NumTris = Generator->Triangles.Num();
		for (int i = 0; i < NumTris; ++i)
		{
			int tid = AppendTriangle(Generator->Triangles[i], 0);
			check(tid == i);
		}
	}


}

void FDynamicMesh3::Copy(const FDynamicMesh3& copy, bool bNormals, bool bColors, bool bUVs, bool bAttributes)
{
	Vertices        = copy.Vertices;
	VertexNormals   = bNormals ? copy.VertexNormals : TOptional<TDynamicVector<FVector3f>>{};
	VertexColors    = bColors  ? copy.VertexColors : TOptional<TDynamicVector<FVector3f>>{};
	VertexUVs       = bUVs     ? copy.VertexUVs : TOptional<TDynamicVector<FVector2f>>{};
	VertexRefCounts = copy.VertexRefCounts;
	VertexEdgeLists = copy.VertexEdgeLists;

	Triangles         = copy.Triangles;
	TriangleEdges     = copy.TriangleEdges;
	TriangleRefCounts = copy.TriangleRefCounts;
	TriangleGroups    = copy.TriangleGroups;
	GroupIDCounter    = copy.GroupIDCounter;

	Edges         = copy.Edges;
	EdgeRefCounts = copy.EdgeRefCounts;

	DiscardAttributes();
	if (bAttributes && copy.HasAttributes())
	{
		EnableAttributes();
		AttributeSet->Copy(*copy.AttributeSet);
	}

	Timestamp = FMath::Max(Timestamp + 1, copy.Timestamp);
	ShapeTimestamp = TopologyTimestamp = Timestamp;
}

void FDynamicMesh3::CompactCopy(const FDynamicMesh3& copy, bool bNormals, bool bColors, bool bUVs, bool bAttributes, FCompactMaps* CompactInfo)
{
	if (copy.IsCompact() && ((!bAttributes || !HasAttributes()) || AttributeSet->IsCompact())) {
		Copy(copy, bNormals, bColors, bUVs, bAttributes);
		if (CompactInfo)
		{
			CompactInfo->SetIdentity(MaxVertexID(), MaxTriangleID());
		}
		return;
	}

	// currently cannot re-use existing attribute buffers
	Clear();

	// use a local map if none passed in
	FCompactMaps LocalMapsVar;
	FCompactMaps* UseMaps = CompactInfo;
	bool bNeedClearTriangleMap = false;
	if (!UseMaps)
	{
		UseMaps = &LocalMapsVar;
		UseMaps->bKeepTriangleMap = bAttributes && copy.HasAttributes();
	}
	else
	{
		// check if we need to temporarily keep the triangle map and clear it after
		bool bNeedTriangleMap = bAttributes && copy.HasAttributes();
		if (bNeedTriangleMap && !UseMaps->bKeepTriangleMap)
		{
			UseMaps->bKeepTriangleMap = true;
			bNeedClearTriangleMap = true;
		}
	}
	UseMaps->Reset();

	FVertexInfo vinfo;
	TArray<int>& mapV = UseMaps->MapV; mapV.SetNumUninitialized(copy.MaxVertexID());

	for (int vid = 0; vid < copy.MaxVertexID(); vid++)
	{
		if (copy.IsVertex(vid))
		{
			copy.GetVertex(vid, vinfo, bNormals, bColors, bUVs);
			mapV[vid] = AppendVertex(vinfo);
		}
		else
		{
			mapV[vid] = -1;
		}
	}

	// [TODO] would be much faster to explicitly copy triangle & edge data structures!!
	if (copy.HasTriangleGroups())
	{
		EnableTriangleGroups(0);
	}

	// need the triangle map to be computed if we have attributes and/or the FCompactMaps flag was set to request it
	bool bNeedsTriangleMap = (bAttributes && copy.HasAttributes()) || UseMaps->bKeepTriangleMap;
	if (bNeedsTriangleMap)
	{
		UseMaps->MapT.SetNumUninitialized(copy.MaxTriangleID());
		for (int tid = 0; tid < copy.MaxTriangleID(); tid++)
		{
			UseMaps->MapT[tid] = -1;
		}
	}
	for (int tid : copy.TriangleIndicesItr())
	{
		FIndex3i t = copy.GetTriangle(tid);
		t = FIndex3i(mapV[t.A], mapV[t.B], mapV[t.C]);
		int g = (copy.HasTriangleGroups()) ? copy.GetTriangleGroup(tid) : InvalidID;
		int NewTID = AppendTriangle(t, g);
		GroupIDCounter = FMath::Max(GroupIDCounter, g + 1);
		if (bNeedsTriangleMap)
		{
			UseMaps->MapT[tid] = NewTID;
		}
	}

	if (bAttributes && copy.HasAttributes())
	{
		EnableAttributes();
		AttributeSet->EnableMatchingAttributes(*copy.Attributes());
		AttributeSet->CompactCopy(*UseMaps, *copy.Attributes());
	}

	if (bNeedClearTriangleMap)
	{
		CompactInfo->ClearTriangleMap();
	}

	Timestamp = FMath::Max(Timestamp + 1, copy.Timestamp);
	ShapeTimestamp = TopologyTimestamp = Timestamp;
}

void FDynamicMesh3::Clear()
{
	*this = FDynamicMesh3();
}

int FDynamicMesh3::GetComponentsFlags() const
{
	int c = 0;
	if (HasVertexNormals())
	{
		c |= (int)EMeshComponents::VertexNormals;
	}
	if (HasVertexColors())
	{
		c |= (int)EMeshComponents::VertexColors;
	}
	if (HasVertexUVs())
	{
		c |= (int)EMeshComponents::VertexUVs;
	}
	if (HasTriangleGroups())
	{
		c |= (int)EMeshComponents::FaceGroups;
	}
	return c;
}

void FDynamicMesh3::EnableMeshComponents(int MeshComponentsFlags)
{
	if (int(EMeshComponents::FaceGroups) & MeshComponentsFlags)
	{
		EnableTriangleGroups(0);
	}
	else
	{
		DiscardTriangleGroups();
	}
	if (int(EMeshComponents::VertexColors) & MeshComponentsFlags)
	{
		EnableVertexColors(FVector3f(1, 1, 1));
	}
	else
	{
		DiscardVertexColors();
	}
	if (int(EMeshComponents::VertexNormals) & MeshComponentsFlags)
	{
		EnableVertexNormals(FVector3f::UnitY());
	}
	else
	{
		DiscardVertexNormals();
	}
	if (int(EMeshComponents::VertexUVs) & MeshComponentsFlags)
	{
		EnableVertexUVs(FVector2f(0, 0));
	}
	else
	{
		DiscardVertexUVs();
	}
}

void FDynamicMesh3::EnableVertexNormals(const FVector3f& InitialNormal)
{
	if (HasVertexNormals())
	{
		return;
	}

	TDynamicVector<FVector3f> NewNormals;
	int NV = MaxVertexID();
	NewNormals.Resize(NV);
	for (int i = 0; i < NV; ++i)
	{
		NewNormals[i] = InitialNormal;
	}
	VertexNormals = MoveTemp(NewNormals);
}

void FDynamicMesh3::DiscardVertexNormals()
{
	VertexNormals.Reset();
}

void FDynamicMesh3::EnableVertexColors(const FVector3f& InitialColor)
{
	if (HasVertexColors())
	{
		return;
	}
	VertexColors = TDynamicVector<FVector3f>();
	int NV = MaxVertexID();
	VertexColors->Resize(NV);
	for (int i = 0; i < NV; ++i)
	{
		VertexColors.GetValue()[i] = InitialColor;
	}
}

void FDynamicMesh3::DiscardVertexColors()
{
	VertexColors.Reset();
}

void FDynamicMesh3::EnableVertexUVs(const FVector2f& InitialUV)
{
	if (HasVertexUVs())
	{
		return;
	}
	VertexUVs = TDynamicVector<FVector2f>();
	int NV = MaxVertexID();
	VertexUVs->Resize(NV);
	for (int i = 0; i < NV; ++i)
	{
		VertexUVs.GetValue()[i] = InitialUV;
	}
}

void FDynamicMesh3::DiscardVertexUVs()
{
		VertexUVs.Reset();
}

void FDynamicMesh3::EnableTriangleGroups(int InitialGroup)
{
	if (HasTriangleGroups())
	{
		return;
	}
	checkSlow(InitialGroup >= 0);
	TriangleGroups = TDynamicVector<int>();
	int NT = MaxTriangleID();
	TriangleGroups->Resize(NT);
	for (int i = 0; i < NT; ++i)
	{
		TriangleGroups.GetValue()[i] = InitialGroup;
	}
	GroupIDCounter = InitialGroup + 1;
}

void FDynamicMesh3::DiscardTriangleGroups()
{
	TriangleGroups.Reset();
	GroupIDCounter = 0;
}

void FDynamicMesh3::EnableAttributes()
{
	if (HasAttributes())
	{
		return;
	}
	AttributeSet = MakeUnique<FDynamicMeshAttributeSet>(this);
	AttributeSet->Initialize(MaxVertexID(), MaxTriangleID());
}

void FDynamicMesh3::DiscardAttributes()
{
	AttributeSet = nullptr;
}

bool FDynamicMesh3::GetVertex(int vID, FVertexInfo& vinfo, bool bWantNormals, bool bWantColors, bool bWantUVs) const
{
	if (VertexRefCounts.IsValid(vID) == false)
	{
		return false;
	}
	vinfo.Position = Vertices[vID];
	vinfo.bHaveN = vinfo.bHaveUV = vinfo.bHaveC = false;
	if (HasVertexNormals() && bWantNormals)
	{
		vinfo.bHaveN = true;
		const TDynamicVector<FVector3f>& NormalVec = VertexNormals.GetValue();
		vinfo.Normal = NormalVec[vID];
	}
	if (HasVertexColors() && bWantColors)
	{
		vinfo.bHaveC = true;
		const TDynamicVector<FVector3f>& ColorVec = VertexColors.GetValue();
		vinfo.Color = ColorVec[vID];
	}
	if (HasVertexUVs() && bWantUVs)
	{
		vinfo.bHaveUV = true;
		const TDynamicVector<FVector2f>& UVVec = VertexUVs.GetValue();
		vinfo.UV = UVVec[vID];
	}
	return true;
}

int FDynamicMesh3::GetMaxVtxEdgeCount() const
{
	int max = 0;
	for (int vid : VertexIndicesItr())
	{
		max = FMath::Max(max, VertexEdgeLists.GetCount(vid));
	}
	return max;
}

FVertexInfo FDynamicMesh3::GetVertexInfo(int i) const
{
	FVertexInfo vi = FVertexInfo();
	vi.Position = GetVertex(i);
	vi.bHaveN = vi.bHaveC = vi.bHaveUV = false;
	if (HasVertexNormals())
	{
		vi.bHaveN = true;
		vi.Normal = GetVertexNormal(i);
	}
	if (HasVertexColors())
	{
		vi.bHaveC = true;
		vi.Color = GetVertexColor(i);
	}
	if (HasVertexUVs())
	{
		vi.bHaveUV = true;
		vi.UV = GetVertexUV(i);
	}
	return vi;
}

FIndex3i FDynamicMesh3::GetTriNeighbourTris(int tID) const
{
	if (TriangleRefCounts.IsValid(tID))
	{
		FIndex3i nbr_t = FIndex3i::Zero();
		for (int j = 0; j < 3; ++j)
		{
			FEdge Edge = Edges[TriangleEdges[tID][j]];
			nbr_t[j] = (Edge.Tri[0] == tID) ? Edge.Tri[1] : Edge.Tri[0];
		}
		return nbr_t;
	}
	else
	{
		return InvalidTriangle;
	}
}


void FDynamicMesh3::GetVertexOneRingTriangles(int VertexID, TArray<int>& TrianglesOut) const
{
	check(VertexRefCounts.IsValid(VertexID));
	VertexEdgeLists.Enumerate(VertexID, [&](int32 EdgeID)
	{
		FIndex2i EdgePair = GetOrderedOneRingEdgeTris(VertexID, EdgeID);
		if (EdgePair.A != InvalidID)
		{
			TrianglesOut.Add(EdgePair.A);
		}
		if (EdgePair.B != InvalidID)
		{
			TrianglesOut.Add(EdgePair.B);
		}
	});
}


void FDynamicMesh3::EnumerateVertexTriangles(int32 VertexID, TFunctionRef<void(int32)> ApplyFunc) const
{
	check(VertexRefCounts.IsValid(VertexID));
	VertexEdgeLists.Enumerate(VertexID, [&](int32 EdgeID)
	{
		FIndex2i EdgePair = GetOrderedOneRingEdgeTris(VertexID, EdgeID);
		if (EdgePair.A != InvalidID)
		{
			ApplyFunc(EdgePair.A);
		}
		if (EdgePair.B != InvalidID)
		{
			ApplyFunc(EdgePair.B);
		}
	});
}


FString FDynamicMesh3::MeshInfoString()
{
	FString VtxString = FString::Printf(TEXT("Vertices count %d max %d  %s  VtxEdges %s"),
		VertexCount(), MaxVertexID(), *VertexRefCounts.UsageStats(), *(VertexEdgeLists.MemoryUsage()));
	FString TriString = FString::Printf(TEXT("Triangles count %d max %d  %s"),
		TriangleCount(), MaxTriangleID(), *TriangleRefCounts.UsageStats());
	FString EdgeString = FString::Printf(TEXT("Edges count %d max %d  %s"),
		EdgeCount(), MaxEdgeID(), *EdgeRefCounts.UsageStats());
	FString AttribString = FString::Printf(TEXT("VtxNormals %d  VtxColors %d  VtxUVs %d  TriGroups %d  Attributes %d"),
		HasVertexNormals(), HasVertexColors(), HasVertexUVs(), HasTriangleGroups(), HasAttributes());
	FString InfoString = FString::Printf(TEXT("Closed %d  Compact %d  Timestamp %d  ShapeTimestamp %d  TopologyTimestamp %d  MaxGroupID %d"),
		GetCachedIsClosed(), IsCompact(), GetTimestamp(), GetShapeTimestamp(), GetTopologyTimestamp(), MaxGroupID());

	return VtxString + "\n" + TriString + "\n" + EdgeString + "\n" + AttribString + "\n" + InfoString;
}

bool FDynamicMesh3::IsSameMesh(const FDynamicMesh3& m2, bool bCheckConnectivity, bool bCheckEdgeIDs,
	bool bCheckNormals, bool bCheckColors, bool bCheckUVs, bool bCheckGroups,
	float Epsilon)
{
	if (VertexCount() != m2.VertexCount())
	{
		return false;
	}
	if (TriangleCount() != m2.TriangleCount())
	{
		return false;
	}
	for (int vid : VertexIndicesItr())
	{
		if (m2.IsVertex(vid) == false || VectorUtil::EpsilonEqual(GetVertex(vid), m2.GetVertex(vid), (double)Epsilon) == false)
		{
			return false;
		}
	}
	for (int tid : TriangleIndicesItr())
	{
		if (m2.IsTriangle(tid) == false || (GetTriangle(tid) != m2.GetTriangle(tid)))
		{
			return false;
		}
	}
	if (bCheckConnectivity)
	{
		for (int eid : EdgeIndicesItr())
		{
			FEdge e = GetEdge(eid);
			int other_eid = m2.FindEdge(e.Vert[0], e.Vert[1]);
			if (other_eid == InvalidID)
			{
				return false;
			}
			FEdge oe = m2.GetEdge(other_eid);
			if (FMath::Min(e.Tri[0], e.Tri[1]) != FMath::Min(oe.Tri[0], oe.Tri[1]) ||
			    FMath::Max(e.Tri[0], e.Tri[1]) != FMath::Max(oe.Tri[0], oe.Tri[1]))
			{
				return false;
			}
		}
	}
	if (bCheckEdgeIDs)
	{
		if (EdgeCount() != m2.EdgeCount())
		{
			return false;
		}
		for (int eid : EdgeIndicesItr())
		{
			if (m2.IsEdge(eid) == false || GetEdge(eid) != m2.GetEdge(eid))
			{
				return false;
			}
		}
	}
	if (bCheckNormals)
	{
		if (HasVertexNormals() != m2.HasVertexNormals())
		{
			return false;
		}
		if (HasVertexNormals())
		{
			for (int vid : VertexIndicesItr())
			{
				if (VectorUtil::EpsilonEqual(GetVertexNormal(vid), m2.GetVertexNormal(vid), Epsilon) == false)
				{
					return false;
				}
			}
		}
	}
	if (bCheckColors)
	{
		if (HasVertexColors() != m2.HasVertexColors())
		{
			return false;
		}
		if (HasVertexColors())
		{
			for (int vid : VertexIndicesItr())
			{
				if (VectorUtil::EpsilonEqual(GetVertexColor(vid), m2.GetVertexColor(vid), Epsilon) == false)
				{
					return false;
				}
			}
		}
	}
	if (bCheckUVs)
	{
		if (HasVertexUVs() != m2.HasVertexUVs())
		{
			return false;
		}
		if (HasVertexUVs())
		{
			for (int vid : VertexIndicesItr())
			{
				if (VectorUtil::EpsilonEqual(GetVertexUV(vid), m2.GetVertexUV(vid), Epsilon) == false)
				{
					return false;
				}
			}
		}
	}
	if (bCheckGroups)
	{
		if (HasTriangleGroups() != m2.HasTriangleGroups())
		{
			return false;
		}
		if (HasTriangleGroups())
		{
			for (int tid : TriangleIndicesItr())
			{
				if (GetTriangleGroup(tid) != m2.GetTriangleGroup(tid))
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool FDynamicMesh3::CheckValidity(FValidityOptions ValidityOptions, EValidityCheckFailMode FailMode) const
{

	TArray<int> triToVtxRefs;
	triToVtxRefs.SetNum(MaxVertexID());

	bool is_ok = true;
	TFunction<void(bool)> CheckOrFailF = [&](bool b)
	{
		is_ok = is_ok && b;
	};
	if (FailMode == EValidityCheckFailMode::Check)
	{
		CheckOrFailF = [&](bool b)
		{
			checkf(b, TEXT("FEdgeLoop::CheckValidity failed!"));
			is_ok = is_ok && b;
		};
	}
	else if (FailMode == EValidityCheckFailMode::Ensure)
	{
		CheckOrFailF = [&](bool b)
		{
			ensureMsgf(b, TEXT("FEdgeLoop::CheckValidity failed!"));
			is_ok = is_ok && b;
		};
	}


	for (int tID : TriangleIndicesItr())
	{
		CheckOrFailF(IsTriangle(tID));
		CheckOrFailF(TriangleRefCounts.GetRefCount(tID) == 1);

		// vertices must exist
		FIndex3i tv = GetTriangle(tID);
		for (int j = 0; j < 3; ++j)
		{
			CheckOrFailF(IsVertex(tv[j]));
			triToVtxRefs[tv[j]] += 1;
		}

		// edges must exist and reference this tri
		FIndex3i e;
		for (int j = 0; j < 3; ++j)
		{
			int a = tv[j], b = tv[(j + 1) % 3];
			e[j] = FindEdge(a, b);
			CheckOrFailF(e[j] != InvalidID);
			CheckOrFailF(EdgeHasTriangle(e[j], tID));
			CheckOrFailF(e[j] == FindEdgeFromTri(a, b, tID));
		}
		CheckOrFailF(e[0] != e[1] && e[0] != e[2] && e[1] != e[2]);

		// tri nbrs must exist and reference this tri, or same edge must be boundary edge
		FIndex3i te = GetTriEdges(tID);
		for (int j = 0; j < 3; ++j)
		{
			int eid = te[j];
			CheckOrFailF(IsEdge(eid));
			int tOther = GetOtherEdgeTriangle(eid, tID);
			if (tOther == InvalidID)
			{
				CheckOrFailF(IsBoundaryTriangle(tID));
				continue;
			}

			CheckOrFailF(TriHasNeighbourTri(tOther, tID) == true);

			// edge must have same two verts as tri for same index
			int a = tv[j], b = tv[(j + 1) % 3];
			FIndex2i ev = GetEdgeV(te[j]);
			CheckOrFailF(IndexUtil::SamePairUnordered(a, b, ev[0], ev[1]));

			// also check that nbr edge has opposite orientation
			if (ValidityOptions.bAllowAdjacentFacesReverseOrientation == false)
			{
				FIndex3i othertv = GetTriangle(tOther);
				int found = IndexUtil::FindTriOrderedEdge(b, a, othertv);
				CheckOrFailF(found != InvalidID);
			}
		}
	}

	if (HasTriangleGroups())
	{
		const TDynamicVector<int>& Groups = TriangleGroups.GetValue();
		// must have a group per triangle ID
		CheckOrFailF(Groups.Num() == MaxTriangleID());
		// group IDs must be in range [0, GroupIDCounter)
		for (int TID : TriangleIndicesItr())
		{
			CheckOrFailF(Groups[TID] >= 0);
			CheckOrFailF(Groups[TID] < GroupIDCounter);
		}
	}


	// edge verts/tris must exist
	for (int eID : EdgeIndicesItr())
	{
		CheckOrFailF(IsEdge(eID));
		CheckOrFailF(EdgeRefCounts.GetRefCount(eID) == 1);
		FIndex2i ev = GetEdgeV(eID);
		FIndex2i et = GetEdgeT(eID);
		CheckOrFailF(IsVertex(ev[0]));
		CheckOrFailF(IsVertex(ev[1]));
		CheckOrFailF(et[0] != InvalidID);
		CheckOrFailF(ev[0] < ev[1]);
		CheckOrFailF(IsTriangle(et[0]));
		if (et[1] != InvalidID)
		{
			CheckOrFailF(IsTriangle(et[1]));
		}
	}

	// verify compact check
	bool is_compact = VertexRefCounts.IsDense();
	if (is_compact)
	{
		for (int vid = 0; vid < (int)Vertices.GetLength(); ++vid)
		{
			CheckOrFailF(VertexRefCounts.IsValid(vid));
		}
	}

	// vertex edges must exist and reference this vert
	for (int vID : VertexIndicesItr())
	{
		CheckOrFailF(IsVertex(vID));

		FVector3d v = GetVertex(vID);
		CheckOrFailF(FMathd::IsNaN(v.SquaredLength()) == false);
		CheckOrFailF(FMathd::IsFinite(v.SquaredLength()));

		for (int edgeid : VertexEdgeLists.Values(vID))
		{
			CheckOrFailF(IsEdge(edgeid));
			CheckOrFailF(EdgeHasVertex(edgeid, vID));

			int otherV = GetOtherEdgeVertex(edgeid, vID);
			int e2 = FindEdge(vID, otherV);
			CheckOrFailF(e2 != InvalidID);
			CheckOrFailF(e2 == edgeid);
			e2 = FindEdge(otherV, vID);
			CheckOrFailF(e2 != InvalidID);
			CheckOrFailF(e2 == edgeid);
		}

		for (int nbr_vid : VtxVerticesItr(vID))
		{
			CheckOrFailF(IsVertex(nbr_vid));
			int edge = FindEdge(vID, nbr_vid);
			CheckOrFailF(IsEdge(edge));
		}

		TArray<int> vTris, vTris2;
		GetVtxTriangles(vID, vTris, false);
		GetVtxTriangles(vID, vTris2, true);
		CheckOrFailF(vTris.Num() == vTris2.Num());
		//System.Console.WriteLine(string.Format("{0} {1} {2}", vID, vTris.Count, GetVtxEdges(vID).Count));
		if (ValidityOptions.bAllowNonManifoldVertices)
		{
			CheckOrFailF(vTris.Num() <= GetVtxEdgeCount(vID));
		}
		else
		{
			CheckOrFailF(vTris.Num() == GetVtxEdgeCount(vID) || vTris.Num() == GetVtxEdgeCount(vID) - 1);
		}
		CheckOrFailF(VertexRefCounts.GetRefCount(vID) == vTris.Num() + 1);
		CheckOrFailF(triToVtxRefs[vID] == vTris.Num());
		for (int tID : vTris)
		{
			CheckOrFailF(TriangleHasVertex(tID, vID));
		}

		// check that edges around vert only references tris above, and reference all of them!
		TArray<int> vRemoveTris(vTris);
		for (int edgeid : VertexEdgeLists.Values(vID))
		{
			FIndex2i edget = GetEdgeT(edgeid);
			CheckOrFailF(vTris.Contains(edget[0]));
			if (edget[1] != InvalidID)
			{
				CheckOrFailF(vTris.Contains(edget[1]));
			}
			vRemoveTris.Remove(edget[0]);
			if (edget[1] != InvalidID)
			{
				vRemoveTris.Remove(edget[1]);
			}
		}
		CheckOrFailF(vRemoveTris.Num() == 0);
	}

	if (HasAttributes())
	{
		CheckOrFailF(Attributes()->CheckValidity(true, FailMode));
	}

	return is_ok;
}









int FDynamicMesh3::AddEdgeInternal(int vA, int vB, int tA, int tB)
{
	if (vB < vA) {
		int t = vB; vB = vA; vA = t;
	}
	int eid = EdgeRefCounts.Allocate();
	Edges.InsertAt(FEdge{{vA, vB},{tA, tB}}, eid);
	VertexEdgeLists.Insert(vA, eid);
	VertexEdgeLists.Insert(vB, eid);
	return eid;
}


int FDynamicMesh3::AddTriangleInternal(int a, int b, int c, int e0, int e1, int e2)
{
	int tid = TriangleRefCounts.Allocate();
	Triangles.InsertAt(FIndex3i(a,b,c), tid);
	TriangleEdges.InsertAt(FIndex3i(e0, e1, e2), tid);
	return tid;
}


int FDynamicMesh3::ReplaceEdgeVertex(int eID, int vOld, int vNew)
{
	FIndex2i& Verts = Edges[eID].Vert;
	int a = Verts[0], b = Verts[1];
	if (a == vOld)
	{
		Verts[0] = FMath::Min(b, vNew);
		Verts[1] = FMath::Max(b, vNew);
		return 0;
	}
	else if (b == vOld)
	{
		Verts[0] = FMath::Min(a, vNew);
		Verts[1] = FMath::Max(a, vNew);
		return 1;
	}
	else
	{
		return -1;
	}
}


int FDynamicMesh3::ReplaceEdgeTriangle(int eID, int tOld, int tNew)
{
	FIndex2i& Tris = Edges[eID].Tri;
	int a = Tris[0], b = Tris[1];
	if (a == tOld) {
		if (tNew == InvalidID)
		{
			Tris[0] = b;
			Tris[1] = InvalidID;
		}
		else
		{
			Tris[0] = tNew;
		}
		return 0;
	}
	else if (b == tOld)
	{
		Tris[1] = tNew;
		return 1;
	}
	else
	{
		return -1;
	}
}

int FDynamicMesh3::ReplaceTriangleEdge(int tID, int eOld, int eNew)
{
	FIndex3i& TriEdgeIDs = TriangleEdges[tID];
	for ( int j = 0 ; j < 3 ; ++j )
	{
		if (TriEdgeIDs[j] == eOld)
		{
			TriEdgeIDs[j] = eNew;
			return j;
		}
	}
	return -1;
}



//! returns edge ID
int FDynamicMesh3::FindTriangleEdge(int tID, int vA, int vB) const
{
	const FIndex3i Triangle = Triangles[tID];
	if (IndexUtil::SamePairUnordered(Triangle[0], Triangle[1], vA, vB)) return TriangleEdges[tID][0];
	if (IndexUtil::SamePairUnordered(Triangle[1], Triangle[2], vA, vB)) return TriangleEdges[tID][1];
	if (IndexUtil::SamePairUnordered(Triangle[2], Triangle[0], vA, vB)) return TriangleEdges[tID][2];
	return InvalidID;
}


int32 FDynamicMesh3::FindEdgeInternal(int32 vA, int32 vB, bool& bIsBoundary) const
{
	// edge vertices must be sorted (min,max), that means we only need one index-check in inner loop.
	int32 vMax = vA, vMin = vB;
	if (vB > vA)
	{
		vMax = vB; vMin = vA;
	}
	return VertexEdgeLists.Find(vMin, [&](int32 eid)
	{
		const FEdge Edge = Edges[eid];
		if (Edge.Vert[1] == vMax)
		{
			bIsBoundary = (Edge.Tri[1] == InvalidID);
			return true;
		}
		return false;
	}, InvalidID);
}



int FDynamicMesh3::FindEdge(int vA, int vB) const
{
	check(IsVertex(vA));
	check(IsVertex(vB));
	if (vA == vB)
	{
		// self-edges are not allowed, and if we fall through to the search below on a self edge we will incorrectly
		// sometimes return an arbitrary edge if queried for a self-edge, due to the optimization of only checking one side of the edge
		return InvalidID;
	}

	// edge vertices must be sorted (min,max),
	//   that means we only need one index-check in inner loop.
	int32 vMax = vA, vMin = vB;
	if (vB > vA)
	{
		vMax = vB; vMin = vA;
	}
	return VertexEdgeLists.Find(vMin, [&](int32 eid)
	{
		return (Edges[eid].Vert[1] == vMax);
	}, InvalidID);

	// this is slower, likely because it creates func<> every time. can we do w/o that?
	//return VertexEdgeLists.Find(vI, (eid) => { return Edges[4 * eid + 1] == vO; }, InvalidID);
}

int FDynamicMesh3::FindEdgeFromTri(int vA, int vB, int tID) const
{
	const FIndex3i& Triangle = Triangles[tID];
	const FIndex3i& TriangleEdgeIDs = TriangleEdges[tID];
	if (IndexUtil::SamePairUnordered(vA, vB, Triangle[0], Triangle[1]))
	{
		return TriangleEdgeIDs[0];
	}
	if (IndexUtil::SamePairUnordered(vA, vB, Triangle[1], Triangle[2]))
	{
		return TriangleEdgeIDs[1];
	}
	if (IndexUtil::SamePairUnordered(vA, vB, Triangle[2], Triangle[0]))
	{
		return TriangleEdgeIDs[2];
	}
	return InvalidID;
}

int FDynamicMesh3::FindEdgeFromTriPair(int TriA, int TriB) const
{
	if (TriangleRefCounts.IsValid(TriA) && TriangleRefCounts.IsValid(TriB))
	{
		for (int j = 0; j < 3; ++j)
		{
			int EdgeID = TriangleEdges[TriA][j];
			const FEdge Edge = Edges[EdgeID];
			int NbrT = (Edge.Tri[0] == TriA) ? Edge.Tri[1] : Edge.Tri[0];
			if (NbrT == TriB)
			{
				return EdgeID;
			}
		}
	}
	return InvalidID;
}


