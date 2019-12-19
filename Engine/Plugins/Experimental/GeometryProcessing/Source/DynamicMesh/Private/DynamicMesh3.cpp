// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "Generators/MeshShapeGenerator.h"
#include "Templates/UniquePtr.h"

// NB: These have to be here until C++17 allows inline variables
constexpr int       FDynamicMesh3::InvalidID;
constexpr int       FDynamicMesh3::NonManifoldID;
constexpr int       FDynamicMesh3::InvalidGroupID;
constexpr FVector3d FDynamicMesh3::InvalidVertex;
constexpr FIndex3i  FDynamicMesh3::InvalidTriangle;
constexpr FIndex2i  FDynamicMesh3::InvalidEdge;

FDynamicMesh3::FDynamicMesh3(bool bWantNormals, bool bWantColors, bool bWantUVs, bool bWantTriGroups)
{
	if ( bWantNormals )   { VertexNormals = TDynamicVector<float>{}; }
	if ( bWantColors )    { VertexColors = TDynamicVector<float>{}; }
	if ( bWantUVs )       { VertexUVs = TDynamicVector<float>{}; }
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

	Vertices = TDynamicVector<double>();
	VertexEdgeLists = FSmallListSet();
	VertexRefCounts = FRefCountVector();
	Triangles = TDynamicVector<int>();
	TriangleEdges = TDynamicVector<int>();
	TriangleRefCounts = FRefCountVector();
	TriangleGroups = TDynamicVector<int>();
	GroupIDCounter = 0;
	Edges = TDynamicVector<int>();
	EdgeRefCounts = FRefCountVector();

	EnableAttributes();
	FDynamicMeshUVOverlay* UVOverlay = Attributes()->PrimaryUV();
	FDynamicMeshNormalOverlay* NormalOverlay = Attributes()->PrimaryNormals();

	int NumVerts = Generator->Vertices.Num();
	for (int i = 0; i < NumVerts; ++i)
	{
		AppendVertex(Generator->Vertices[i]);
	}
	int NumUVs = Generator->UVs.Num();
	for (int i = 0; i < NumUVs; ++i)
	{
		UVOverlay->AppendElement(Generator->UVs[i], Generator->UVParentVertex[i]);
	}
	int NumNormals = Generator->Normals.Num();
	for (int i = 0; i < NumNormals; ++i)
	{
		NormalOverlay->AppendElement(Generator->Normals[i], Generator->NormalParentVertex[i]);
	}

	int NumTris = Generator->Triangles.Num();
	for (int i = 0; i < NumTris; ++i)
	{
		int tid = AppendTriangle(Generator->Triangles[i], Generator->TrianglePolygonIDs[i]);
		check(tid == i);
		UVOverlay->SetTriangle(tid, Generator->TriangleUVs[i]);
		NormalOverlay->SetTriangle(tid, Generator->TriangleNormals[i]);
	}
}

void FDynamicMesh3::Copy(const FDynamicMesh3& copy, bool bNormals, bool bColors, bool bUVs, bool bAttributes)
{
	Vertices        = copy.Vertices;
	VertexNormals   = bNormals ? copy.VertexNormals : TOptional<TDynamicVector<float>>{};
	VertexColors    = bColors  ? copy.VertexColors : TOptional<TDynamicVector<float>>{};
	VertexUVs       = bUVs     ? copy.VertexUVs : TOptional<TDynamicVector<float>>{};
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

void FDynamicMesh3::EnableVertexNormals(const FVector3f& InitialNormal)
{
	if (HasVertexNormals())
	{
		return;
	}
	VertexNormals = TDynamicVector<float>();
	int NV = MaxVertexID();
	VertexNormals->Resize(3 * NV);
	for (int i = 0; i < NV; ++i)
	{
		int vi = 3 * i;
		VertexNormals.GetValue()[vi] = InitialNormal.X;
		VertexNormals.GetValue()[vi + 1] = InitialNormal.Y;
		VertexNormals.GetValue()[vi + 2] = InitialNormal.Z;
	}
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
	VertexColors = TDynamicVector<float>();
	int NV = MaxVertexID();
	VertexColors->Resize(3 * NV);
	for (int i = 0; i < NV; ++i)
	{
		int vi = 3 * i;
		VertexColors.GetValue()[vi] = InitialColor.X;
		VertexColors.GetValue()[vi + 1] = InitialColor.Y;
		VertexColors.GetValue()[vi + 2] = InitialColor.Z;
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
	VertexUVs = TDynamicVector<float>();
	int NV = MaxVertexID();
	VertexUVs->Resize(2 * NV);
	for (int i = 0; i < NV; ++i)
	{
		int vi = 2 * i;
		VertexUVs.GetValue()[vi] = InitialUV.X;
		VertexUVs.GetValue()[vi + 1] = InitialUV.Y;
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
	TriangleGroups = TDynamicVector<int>();
	int NT = MaxTriangleID();
	TriangleGroups->Resize(NT);
	for (int i = 0; i < NT; ++i)
	{
		TriangleGroups.GetValue()[i] = InitialGroup;
	}
	GroupIDCounter = 0;
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
	int vi = 3 * vID;
	vinfo.Position = FVector3d(Vertices[vi], Vertices[vi + 1], Vertices[vi + 2]);
	vinfo.bHaveN = vinfo.bHaveUV = vinfo.bHaveC = false;
	if (HasVertexNormals() && bWantNormals)
	{
		vinfo.bHaveN = true;
		const TDynamicVector<float>& NormalVec = VertexNormals.GetValue();
		vinfo.Normal = FVector3f(NormalVec[vi], NormalVec[vi + 1], NormalVec[vi + 2]);
	}
	if (HasVertexColors() && bWantColors)
	{
		vinfo.bHaveC = true;
		const TDynamicVector<float>& ColorVec = VertexColors.GetValue();
		vinfo.Color = FVector3f(ColorVec[vi], ColorVec[vi + 1], ColorVec[vi + 2]);
	}
	if (HasVertexUVs() && bWantUVs)
	{
		vinfo.bHaveUV = true;
		const TDynamicVector<float>& UVVec = VertexUVs.GetValue();
		vinfo.UV = FVector2f(UVVec[2*vID], UVVec[2*vID + 1]);
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
		int tei = 3 * tID;
		FIndex3i nbr_t = FIndex3i::Zero();
		for (int j = 0; j < 3; ++j)
		{
			int ei = 4 * TriangleEdges[tei + j];
			nbr_t[j] = (Edges[ei + 2] == tID) ? Edges[ei + 3] : Edges[ei + 2];
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
	for (int EdgeID : VertexEdgeLists.Values(VertexID))
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
	}
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
			FIndex4i e = GetEdge(eid);
			int other_eid = m2.FindEdge(e[0], e[1]);
			if (other_eid == InvalidID)
			{
				return false;
			}
			FIndex4i oe = m2.GetEdge(other_eid);
			if (FMath::Min(e[2], e[3]) != FMath::Min(oe[2], oe[3]) || FMath::Max(e[2], e[3]) != FMath::Max(oe[2], oe[3]))
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

bool FDynamicMesh3::CheckValidity(bool bAllowNonManifoldVertices, EValidityCheckFailMode FailMode) const
{

	TArray<int> triToVtxRefs;
	triToVtxRefs.SetNum(MaxVertexID());
	//int[] triToVtxRefs = new int[this.MaxVertexID];

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
			FIndex3i othertv = GetTriangle(tOther);
			int found = IndexUtil::FindTriOrderedEdge(b, a, othertv);
			CheckOrFailF(found != InvalidID);
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
		for (int vid = 0; vid < (int)Vertices.GetLength() / 3; ++vid) 
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
		if (bAllowNonManifoldVertices)
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
	int i = 4 * eid;
	Edges.InsertAt(vA, i);
	Edges.InsertAt(vB, i + 1);
	Edges.InsertAt(tA, i + 2);
	Edges.InsertAt(tB, i + 3);

	VertexEdgeLists.Insert(vA, eid);
	VertexEdgeLists.Insert(vB, eid);
	return eid;
}


int FDynamicMesh3::AddTriangleInternal(int a, int b, int c, int e0, int e1, int e2)
{
	int tid = TriangleRefCounts.Allocate();
	int i = 3 * tid;
	Triangles.InsertAt(c, i + 2);
	Triangles.InsertAt(b, i + 1);
	Triangles.InsertAt(a, i);
	TriangleEdges.InsertAt(e2, i + 2);
	TriangleEdges.InsertAt(e1, i + 1);
	TriangleEdges.InsertAt(e0, i + 0);
	return tid;
}


int FDynamicMesh3::ReplaceEdgeVertex(int eID, int vOld, int vNew)
{
	int i = 4 * eID;
	int a = Edges[i], b = Edges[i + 1];
	if (a == vOld) 
	{
		Edges[i] = FMath::Min(b, vNew);
		Edges[i + 1] = FMath::Max(b, vNew);
		return 0;
	}
	else if (b == vOld) 
	{
		Edges[i] = FMath::Min(a, vNew);
		Edges[i + 1] = FMath::Max(a, vNew);
		return 1;
	}
	else
	{
		return -1;
	}
}


int FDynamicMesh3::ReplaceEdgeTriangle(int eID, int tOld, int tNew)
{
	int i = 4 * eID;
	int a = Edges[i + 2], b = Edges[i + 3];
	if (a == tOld) {
		if (tNew == InvalidID) 
		{
			Edges[i + 2] = b;
			Edges[i + 3] = InvalidID;
		}
		else
		{
			Edges[i + 2] = tNew;
		}
		return 0;
	}
	else if (b == tOld) 
	{
		Edges[i + 3] = tNew;
		return 1;
	}
	else
	{
		return -1;
	}
}

int FDynamicMesh3::ReplaceTriangleEdge(int tID, int eOld, int eNew)
{
	int i = 3 * tID;
	if (TriangleEdges[i] == eOld) 
	{
		TriangleEdges[i] = eNew;
		return 0;
	}
	else if (TriangleEdges[i + 1] == eOld) 
	{
		TriangleEdges[i + 1] = eNew;
		return 1;
	}
	else if (TriangleEdges[i + 2] == eOld) 
	{
		TriangleEdges[i + 2] = eNew;
		return 2;
	}
	else
	{
		return -1;
	}
}



//! returns edge ID
int FDynamicMesh3::FindTriangleEdge(int tID, int vA, int vB) const
{
	int i = 3 * tID;
	int tv0 = Triangles[i], tv1 = Triangles[i + 1];
	if (IndexUtil::SamePairUnordered(tv0, tv1, vA, vB)) return TriangleEdges[3 * tID];
	int tv2 = Triangles[i + 2];
	if (IndexUtil::SamePairUnordered(tv1, tv2, vA, vB)) return TriangleEdges[3 * tID + 1];
	if (IndexUtil::SamePairUnordered(tv2, tv0, vA, vB)) return TriangleEdges[3 * tID + 2];
	return InvalidID;
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

	// [RMS] edge vertices must be sorted (min,max),
	//   that means we only need one index-check in inner loop.
	//   commented out code is robust to incorrect ordering, but slower.
	int vO = FMath::Max(vA, vB);
	int vI = FMath::Min(vA, vB);
	for (int eid : VertexEdgeLists.Values(vI)) 
	{
		if (Edges[4 * eid + 1] == vO)
		{
			//if (DoesEdgeHaveVertex(eid, vO))
			return eid;
		}
	}
	return InvalidID;

	// this is slower, likely because it creates func<> every time. can we do w/o that?
	//return VertexEdgeLists.Find(vI, (eid) => { return Edges[4 * eid + 1] == vO; }, InvalidID);
}

int FDynamicMesh3::FindEdgeFromTri(int vA, int vB, int tID) const
{
	int i = 3 * tID;
	int t0 = Triangles[i], t1 = Triangles[i + 1];
	if (IndexUtil::SamePairUnordered(vA, vB, t0, t1))
	{
		return TriangleEdges[i];
	}
	int t2 = Triangles[i + 2];
	if (IndexUtil::SamePairUnordered(vA, vB, t1, t2))
	{
		return TriangleEdges[i + 1];
	}
	if (IndexUtil::SamePairUnordered(vA, vB, t2, t0))
	{
		return TriangleEdges[i + 2];
	}
	return InvalidID;
}

int FDynamicMesh3::FindEdgeFromTriPair(int TriA, int TriB) const
{
	if (TriangleRefCounts.IsValid(TriA) && TriangleRefCounts.IsValid(TriB))
	{
		int AI = 3 * TriA, BI = 3 * TriB;
		for (int j = 0; j < 3; ++j)
		{
			int EdgeID = TriangleEdges[AI + j];
			int EdgeIndex = 4 * EdgeID;
			int NbrT = (Edges[EdgeIndex + 2] == TriA) ? Edges[EdgeIndex + 3] : Edges[EdgeIndex + 2];
			if (NbrT == TriB)
			{
				return EdgeID;
			}
		}
	}
	return InvalidID;
}


