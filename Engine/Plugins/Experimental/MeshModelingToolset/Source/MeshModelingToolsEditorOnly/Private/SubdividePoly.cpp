// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubdividePoly.h"
#include "GroupTopology.h"
#include "MeshNormals.h"

// OpenSubdiv needs M_PI defined
#ifndef M_PI
#define M_PI PI
#define LOCAL_M_PI 1
#endif

#pragma warning(push, 0)     
#include "far/topologyRefiner.h"
#include "far/topologyDescriptor.h"
#include "far/primvarRefiner.h"
#pragma warning(pop)     

#if LOCAL_M_PI
#undef M_PI
#endif

namespace SubdividePolyLocal
{
	// Vertices with positions and normals. Used for interpolating from polymesh to subdivision surface mesh.
	struct SubdVertex
	{
		FVertexInfo VertexInfo;

		void Clear()
		{
			VertexInfo = FVertexInfo();
		}

		void AddWithWeight(SubdVertex const& Src, float Weight)
		{
			VertexInfo.Position += Weight * Src.VertexInfo.Position;
			VertexInfo.Normal += Weight * Src.VertexInfo.Normal;
			VertexInfo.bHaveN = Src.VertexInfo.bHaveN;
			// TODO: add support for vertex color
		}
	};

	// Vertex with only UV data. This is used for "face-varying" interpolation, i.e. vertices can have multiple UV 
	// values if they are incident on triangles in different UV islands.
	struct SubdUVVertex
	{
		FVector2f VertexUV;

		SubdUVVertex(const FVector2f& InVertexUV) :
			VertexUV{ InVertexUV }
		{}

		void Clear()
		{
			VertexUV = FVector2f();
		}

		void AddWithWeight(SubdUVVertex const& Src, float Weight)
		{
			VertexUV += Weight * Src.VertexUV;
		}
	};


	// Compute the average of all normals corresponding to the given VertexID
	FVector3f GetAverageVertexNormalFromOverlay(const FDynamicMeshNormalOverlay* NormalOverlay,
												int VertexID)
	{
		TArray<int> NormalElements;
		NormalOverlay->GetVertexElements(VertexID, NormalElements);

		FVector3f SumElements{ 0.0f, 0.0f, 0.0f };
		float ElementCount = 0.0f;
		for (int ElementID : NormalElements)
		{
			SumElements += NormalOverlay->GetElement(ElementID);
			ElementCount += 1.0f;
		}

		return SumElements / ElementCount;
	}

	// Get the indices of GroupTopology "Corners" for a particular group boundary.
	void GetBoundaryCorners(const FGroupTopology::FGroupBoundary& Boundary,
							const FGroupTopology& Topology,
							TArray<int>& Corners)
	{
		int FirstEdgeIndex = Boundary.GroupEdges[0];
		Corners.Add(Topology.Edges[FirstEdgeIndex].EndpointCorners[0]);
		Corners.Add(Topology.Edges[FirstEdgeIndex].EndpointCorners[1]);

		int NextEdgeIndex = Boundary.GroupEdges[1];
		FIndex2i NextEdgeCorners = Topology.Edges[NextEdgeIndex].EndpointCorners;
		if (Corners[1] != NextEdgeCorners[0] && Corners[1] != NextEdgeCorners[1])
		{
			Swap(Corners[0], Corners[1]);
			check(Corners[1] == NextEdgeCorners[0] || Corners[1] == NextEdgeCorners[1]);
		}

		for (int i = 1; i < Boundary.GroupEdges.Num() - 1; ++i)
		{
			int EdgeIndex = Boundary.GroupEdges[i];
			FIndex2i CurrEdgeCorners = Topology.Edges[EdgeIndex].EndpointCorners;
			if (Corners.Last() == CurrEdgeCorners[0])
			{
				Corners.Add(CurrEdgeCorners[1]);
			}
			else
			{
				check(Corners.Last() == CurrEdgeCorners[1]);
				Corners.Add(CurrEdgeCorners[0]);
			}
		}
	}

	// Treat the given FGroupTopology as a polygonal mesh, and get its vertices
	void GetGroupPolyMeshVertices(const FDynamicMesh3& Mesh,
								  const FGroupTopology& Topology,
								  bool bGetVertexNormals,
								  TArray<SubdVertex>& OutVertices )
	{
		for (const FGroupTopology::FCorner& Corner : Topology.Corners)
		{
			SubdVertex Vertex;
			bool bWantNormals = bGetVertexNormals && Mesh.HasVertexNormals();
			constexpr bool bWantColors = false;
			constexpr bool bWantVertexUVs = false;
			Mesh.GetVertex(Corner.VertexID, Vertex.VertexInfo, bWantNormals, bWantColors, bWantVertexUVs);

			if (bGetVertexNormals && !Vertex.VertexInfo.bHaveN)
			{
				// We want vertex normals but the mesh doesn't have them. See if we can get them from an overlay.
				if (Mesh.HasAttributes() && (Mesh.Attributes()->NumNormalLayers() > 0))
				{
					Vertex.VertexInfo.Normal = GetAverageVertexNormalFromOverlay(Mesh.Attributes()->PrimaryNormals(), 
																				 Corner.VertexID);
					Vertex.VertexInfo.bHaveN = true;
				}
				else
				{
					Vertex.VertexInfo.Normal = { 0,1,0 };
					Vertex.VertexInfo.bHaveN = true;
				}
			}

			OutVertices.Add(Vertex);
		}
	}


	// Given a group and a vertex ID, return:
	// - a triangle in the group with one of its corners equal to vertex ID
	// - the (0-2) triangle corner index corresponding to the given vertex ID
	bool FindTriangleVertex(const FGroupTopology::FGroup& Group,
							int VertexID,
							const FDynamicMesh3& Mesh,
							TTuple<int, int>& OutTriangleVertex)
	{
		for (int Tri : Group.Triangles)
		{
			FIndex3i TriVertices = Mesh.GetTriangle(Tri);
			for (int i = 0; i < 3; ++i)
			{
				if (TriVertices[i] == VertexID)
				{
					OutTriangleVertex = TTuple<int, int>{ Tri, i };
					return true;
				}
			}
		}
		return false;
	}

	// Treating the GroupTopology as a polygonal mesh, get its "face-varying" UV coordinates. Assumes that each 
	// polygonal face has no UV seams cutting through it, but that UV seams might exist at polygon boundaries.
	bool GetGroupPolyMeshUVs(const FGroupTopology& Topology,
							 const FDynamicMesh3& Mesh,
							 const FDynamicMeshUVOverlay* UVOverlay,
							 TArray<SubdUVVertex>& OutUVs)		// for each group, all the uvs on the group corners
	{
		for (const FGroupTopology::FGroup& Group : Topology.Groups)
		{
			check(Group.Boundaries.Num() == 1);
			check(Group.Triangles.Num() > 0);
			const FGroupTopology::FGroupBoundary& Bdry = Group.Boundaries[0];

			TArray<int> Corners;
			GetBoundaryCorners(Bdry, Topology, Corners);

			for (int CornerID : Corners)
			{
				int CornerVertexID = Topology.Corners[CornerID].VertexID;

				// Find a triangle in the group that has a vertex ID equal to the given corner
				TTuple<int, int> TriangleVertex;
				if (FindTriangleVertex(Group, CornerVertexID, Mesh, TriangleVertex))
				{
					int TriangleID = TriangleVertex.Get<0>();
					int TriVertexIndex = TriangleVertex.Get<1>();	// The (0,1,2) index of the polygon corner wrt the triangle

					TStaticArray<FVector2f, 3> TriangleUVs;
					UVOverlay->GetTriElements(TriangleID, TriangleUVs[0], TriangleUVs[1], TriangleUVs[2]);
					FVector2f CornerUV = TriangleUVs[TriVertexIndex];

					OutUVs.Add(SubdUVVertex{ CornerUV });
				}
				else
				{
					return false;
				}
			}
		}

		return true;
	}


	void InitializeOverlayToFaceVertexUVs(FDynamicMeshUVOverlay* UVOverlay, const TArray<FVector2f>& UVs)
	{
		const FDynamicMesh3* Mesh = UVOverlay->GetParentMesh();
		check(Mesh->IsCompact());

		int NumTriangles = Mesh->TriangleCount();
		check(NumTriangles == Mesh->MaxTriangleID());
		check(UVs.Num() == 3 * NumTriangles);

		UVOverlay->ClearElements();
		UVOverlay->InitializeTriangles(NumTriangles);

		for (int TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
		{
			FIndex3i Tri{ 3 * TriangleIndex + 0, 3 * TriangleIndex + 1,	3 * TriangleIndex + 2 };
			UVOverlay->AppendElement(UVs[Tri[0]]);
			UVOverlay->AppendElement(UVs[Tri[1]]);
			UVOverlay->AppendElement(UVs[Tri[2]]);
			UVOverlay->SetTriangle(TriangleIndex, Tri);
		}
	}


}	// namespace SubdivisionSurfaceLocal


class FSubdividePoly::RefinerImpl
{
public:
	OpenSubdiv::Far::TopologyRefiner* TopologyRefiner = nullptr;
};

FSubdividePoly::FSubdividePoly(const FGroupTopology& InTopology,
										 const FDynamicMesh3& InOriginalMesh,
										 int InLevel) :
	GroupTopology(InTopology)
	, OriginalMesh(InOriginalMesh)
	, Level(InLevel)
{
	Refiner = MakeUnique<RefinerImpl>();
}

FSubdividePoly::~FSubdividePoly()
{
	if (Refiner && Refiner->TopologyRefiner)
	{
		// This was created by TopologyRefinerFactory; looks like we are responsible for cleaning it up.
		delete Refiner->TopologyRefiner;
	}
	Refiner = nullptr;
}


bool FSubdividePoly::ComputeTopologySubdivision()
{
	TArray<int> BoundaryVertsPerFace;
	TArray<int> NumVertsPerFace;
	int TotalNumVertices = 0;

	for (const FGroupTopology::FGroup& Group : GroupTopology.Groups)
	{
		if (Group.Boundaries.Num() != 1)
		{
			return false;
		}

		const FGroupTopology::FGroupBoundary& Boundary = Group.Boundaries[0];
		if (Boundary.GroupEdges.Num() < 2)
		{
			return false;
		}

		TArray<int> Corners;
		SubdividePolyLocal::GetBoundaryCorners(Boundary, GroupTopology, Corners);

		NumVertsPerFace.Add(Corners.Num());
		TotalNumVertices += Corners.Num();
		BoundaryVertsPerFace.Append(Corners);
	}

	OpenSubdiv::Far::TopologyDescriptor Descriptor;
	Descriptor.numVertices = TotalNumVertices;
	Descriptor.numFaces = GroupTopology.Groups.Num();
	Descriptor.numVertsPerFace = NumVertsPerFace.GetData();		// taking pointer to local memory seems to be ok?
	Descriptor.vertIndicesPerFace = BoundaryVertsPerFace.GetData();

	OpenSubdiv::Far::TopologyDescriptor::FVarChannel UVChannel;
	TArray<int> Buffer;
	if (UVComputationMethod == ESubdivisionOutputUVs::Interpolated)
	{
		UVChannel.numValues = BoundaryVertsPerFace.Num();
		Buffer.SetNum(UVChannel.numValues);
		for (int i = 0; i < UVChannel.numValues; ++i)
		{
			Buffer[i] = i;
		}
		UVChannel.valueIndices = Buffer.GetData();
		Descriptor.numFVarChannels = 1;
		Descriptor.fvarChannels = &UVChannel;		// taking a pointer to local memory seems to be ok?
	}
	else
	{
		Descriptor.numFVarChannels = 0;
	}

	using RefinerFactory = OpenSubdiv::Far::TopologyRefinerFactory<OpenSubdiv::Far::TopologyDescriptor>;
	RefinerFactory::Options RefinerOptions;

	RefinerOptions.schemeOptions.SetVtxBoundaryInterpolation(OpenSubdiv::Sdc::Options::VTX_BOUNDARY_EDGE_AND_CORNER);

	Refiner->TopologyRefiner = RefinerFactory::Create(Descriptor, RefinerOptions);

	if (Refiner->TopologyRefiner == nullptr)
	{
		return false;
	}

	Refiner->TopologyRefiner->RefineUniform(OpenSubdiv::Far::TopologyRefiner::UniformOptions(Level));

	return true;
}


bool FSubdividePoly::ComputeSubdividedMesh(FDynamicMesh3& OutMesh)
{
	if (!Refiner || !(Refiner->TopologyRefiner))
	{
		return false;
	}

	OpenSubdiv::Far::PrimvarRefiner Interpolator(*Refiner->TopologyRefiner);

	//
	// Interpolate vertex positions from initial Group poly mesh down to refinement level
	// 
	TArray<SubdividePolyLocal::SubdVertex> SourceVertices;
	bool bInterpolateVertexNormals = (NormalComputationMethod == ESubdivisionOutputNormals::Interpolated);
	GetGroupPolyMeshVertices(OriginalMesh, GroupTopology, bInterpolateVertexNormals, SourceVertices);

	TArray<SubdividePolyLocal::SubdVertex> RefinedVertices;
	for (int CurrentLevel = 1; CurrentLevel <= Level; ++CurrentLevel)
	{
		// TODO: Don't keep resizing -- preallocate one big buffer and move through it
		RefinedVertices.SetNumUninitialized(Refiner->TopologyRefiner->GetLevel(CurrentLevel).GetNumVertices());
		SubdividePolyLocal::SubdVertex* s = SourceVertices.GetData();
		SubdividePolyLocal::SubdVertex* d = RefinedVertices.GetData();
		Interpolator.Interpolate(CurrentLevel, s, d);
		SourceVertices = RefinedVertices;
	}

	//
	// Interpolate face group IDs
	// 
	TArray<int> SourceGroupIDs;
	for (const FGroupTopology::FGroup& Group : GroupTopology.Groups)
	{
		SourceGroupIDs.Add(Group.GroupID);
	}

	TArray<int> RefinedGroupIDs;
	for (int CurrentLevel = 1; CurrentLevel <= Level; ++CurrentLevel)
	{
		// TODO: Don't keep resizing -- preallocate one big buffer and move through it
		RefinedGroupIDs.SetNumUninitialized(Refiner->TopologyRefiner->GetLevel(CurrentLevel).GetNumFaces());
		int* s = SourceGroupIDs.GetData();
		int* d = RefinedGroupIDs.GetData();
		Interpolator.InterpolateFaceUniform(CurrentLevel, s, d);
		SourceGroupIDs = RefinedGroupIDs;
	}


	//
	// Interpolate UVs
	// 
	TArray<SubdividePolyLocal::SubdUVVertex> RefinedUVs;

	if (UVComputationMethod == ESubdivisionOutputUVs::Interpolated)
	{
		if (!OriginalMesh.HasAttributes() || !OriginalMesh.Attributes()->PrimaryUV())
		{
			return false;
		}

		TArray<SubdividePolyLocal::SubdUVVertex> SourceUVs;
		bool bGetUVsOK = SubdividePolyLocal::GetGroupPolyMeshUVs(GroupTopology,
																	  OriginalMesh,
																	  OriginalMesh.Attributes()->PrimaryUV(), 
																	  SourceUVs);

		if (!bGetUVsOK)
		{
			return false;
		}

		check(SourceUVs.Num() == Refiner->TopologyRefiner->GetLevel(0).GetNumFaceVertices());

		for (int CurrentLevel = 1; CurrentLevel <= Level; ++CurrentLevel)
		{
			// TODO: Don't keep resizing -- preallocate one big buffer and move through it
			RefinedUVs.SetNumUninitialized(Refiner->TopologyRefiner->GetLevel(CurrentLevel).GetNumFVarValues());
			SubdividePolyLocal::SubdUVVertex* s = SourceUVs.GetData();
			SubdividePolyLocal::SubdUVVertex* d = RefinedUVs.GetData();
			Interpolator.InterpolateFaceVarying(CurrentLevel, s, d);
			SourceUVs = RefinedUVs;
		}
	}

	// Now transfer to output mesh
	OutMesh.Clear();

	OutMesh.EnableTriangleGroups();
	if (bInterpolateVertexNormals)
	{
		OutMesh.EnableVertexNormals(FVector3f{ 0,0,0 });
	}
	if ((NormalComputationMethod != ESubdivisionOutputNormals::None) || (UVComputationMethod != ESubdivisionOutputUVs::None))
	{
		OutMesh.EnableAttributes();
	}

	// Add the vertices
	// TODO: Can we resize the Mesh vertex array up front and then populate it?
	for (const SubdividePolyLocal::SubdVertex& V : RefinedVertices)
	{
		OutMesh.AppendVertex(V.VertexInfo);
	}

	const OpenSubdiv::Far::TopologyLevel& FinalLevel = Refiner->TopologyRefiner->GetLevel(Level);

	check(UVComputationMethod != ESubdivisionOutputUVs::Interpolated || FinalLevel.GetNumFVarValues() == RefinedUVs.Num());
	check(FinalLevel.GetNumFaces() == RefinedGroupIDs.Num());

	// Add the faces (manually triangulate the output here)

	int NumFaceVertices = 0;
	TArray<FVector2f> TriangleUVs;

	for (int FaceID = 0; FaceID < FinalLevel.GetNumFaces(); ++FaceID)
	{
		int GroupID = RefinedGroupIDs[FaceID];

		OpenSubdiv::Far::ConstIndexArray Face = FinalLevel.GetFaceVertices(FaceID);
		NumFaceVertices += Face.size();

		check(Face.size() == 4);

		FIndex3i TriA{ Face[0], Face[1], Face[3] };
		FIndex3i TriB{ Face[2], Face[3], Face[1] };
		OutMesh.AppendTriangle(TriA, GroupID);
		OutMesh.AppendTriangle(TriB, GroupID);

		if (UVComputationMethod == ESubdivisionOutputUVs::Interpolated)
		{
			OpenSubdiv::Far::ConstIndexArray FaceUVIndices = FinalLevel.GetFaceFVarValues(FaceID);

			FIndex3i UVTriA{ FaceUVIndices[0], FaceUVIndices[1], FaceUVIndices[3] };
			TriangleUVs.Add(RefinedUVs[UVTriA[0]].VertexUV);
			TriangleUVs.Add(RefinedUVs[UVTriA[1]].VertexUV);
			TriangleUVs.Add(RefinedUVs[UVTriA[2]].VertexUV);

			FIndex3i UVTriB{ FaceUVIndices[2], FaceUVIndices[3], FaceUVIndices[1] };
			TriangleUVs.Add(RefinedUVs[UVTriB[0]].VertexUV);
			TriangleUVs.Add(RefinedUVs[UVTriB[1]].VertexUV);
			TriangleUVs.Add(RefinedUVs[UVTriB[2]].VertexUV);
		}
	}

	if (NormalComputationMethod != ESubdivisionOutputNormals::None)
	{
		const bool bUseExistingMeshVertexNormals = (NormalComputationMethod == ESubdivisionOutputNormals::Interpolated);
		FMeshNormals::InitializeOverlayToPerVertexNormals(OutMesh.Attributes()->PrimaryNormals(), bUseExistingMeshVertexNormals);
	}

	if (UVComputationMethod == ESubdivisionOutputUVs::Interpolated)
	{
		SubdividePolyLocal::InitializeOverlayToFaceVertexUVs(OutMesh.Attributes()->PrimaryUV(), TriangleUVs);
	}

	return true;
}

