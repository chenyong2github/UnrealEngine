// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParameterizationOps/ParameterizeMeshOp.h"

#include "DynamicMeshAttributeSet.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "ProxyLODParameterization.h"
#include "Selections/MeshConnectedComponents.h"

#include "Parameterization/MeshLocalParam.h"
#include "DynamicMeshAABBTree3.h"
#include "MeshNormals.h"
#include "DynamicSubmesh3.h"




FParameterizeMeshOp::FLinearMesh::FLinearMesh(const FDynamicMesh3& Mesh, const bool bRespectPolygroups)
{

	TArray<FVector>& Positions = this->VertexBuffer;

	// Temporary maps used during construction.

	TArray<int32> TriFromID;
	TArray<int32> TriToID;
	TArray<int32> VertFromID;

	// Compute the mapping from triangle ID to triangle number
	{
		const int32 MaxTriID = Mesh.MaxTriangleID(); // really +1.. all TriID < MaxTriID
		const int32 NumTris  = Mesh.TriangleCount();

		// reserve space and add elements 
		TriFromID.Empty(MaxTriID);
		TriFromID.AddUninitialized(MaxTriID);

		// reserve space
		TriToID.Empty(NumTris);

		int32 count = 0;
		for (int TriID : Mesh.TriangleIndicesItr())
		{
			TriToID.Add(TriID);
			TriFromID[TriID] = count;
			count++;
		}
	}

	// Compute the mapping from vertex ID to vertex number
	{
		const int32 MaxVertID = Mesh.MaxVertexID();
		const int32 NumVerts  = Mesh.VertexCount();

		// reserve space and add elements
		VertFromID.Empty(MaxVertID);
		VertFromID.AddUninitialized(MaxVertID);

		// reserve space
		VertToID.Empty(NumVerts);

		int32 count = 0;
		for (int VtxID : Mesh.VertexIndicesItr())
		{
			VertToID.Add(VtxID);
			VertFromID[VtxID] = count;
			count++;
		}
	}

	// Fill the vertex buffer
	{
		int32 NumVerts = Mesh.VertexCount();
		Positions.Empty(NumVerts);

		for (const auto& Vertex : Mesh.VerticesItr())
		{
			FVector Pos(Vertex.X, Vertex.Y, Vertex.Z);
			Positions.Add(Pos);
		}
	}

	const int32 NumTris = Mesh.TriangleCount();

	// Fill the index buffer
	{
		IndexBuffer.Empty(NumTris * 3);
		for (const auto& Tri : Mesh.TrianglesItr())
		{
			for (int i = 0; i < 3; ++i)
			{
				int VtxID = Tri[i];
				int32 RemapVtx = VertFromID[VtxID];
				IndexBuffer.Add(RemapVtx);
			}
		}

	}


	// For each edge on each triangle.
	AdjacencyBuffer.Empty(NumTris * 3);

	// Create Adjacency - create boundaries in adjacency at polygroup boundaries if requested.
	if (bRespectPolygroups && Mesh.HasTriangleGroups())
	{
		for (int TriID : Mesh.TriangleIndicesItr())
		{
			FIndex3i NbrTris = Mesh.GetTriNeighbourTris(TriID);
			for (int32 i = 0; i < 3; ++i)
			{
				const int32 NbrID = NbrTris[i];

				int32 RemapNbrID = FDynamicMesh3::InvalidID;
				if (NbrID != FDynamicMesh3::InvalidID)
				{
					const int32 CurTriGroup = Mesh.GetTriangleGroup(TriID);
					const int32 NbrTriGroup = Mesh.GetTriangleGroup(NbrID);

					RemapNbrID = (CurTriGroup == NbrTriGroup) ? TriFromID[NbrID] : FDynamicMesh3::InvalidID;
				}

				AdjacencyBuffer.Add(RemapNbrID);
			}
		}

	}
	else  // compute the adjacency using only the mesh connectivity.
	{
		for (int TriID : Mesh.TriangleIndicesItr())
		{
			FIndex3i NbrTris = Mesh.GetTriNeighbourTris(TriID);
			for (int32 i = 0; i < 3; ++i)
			{
				int32 NbrID = NbrTris[i];

				int32 RemapNbrID = (NbrID != FDynamicMesh3::InvalidID) ? TriFromID[NbrID] : FDynamicMesh3::InvalidID;
				AdjacencyBuffer.Add(RemapNbrID);
			}
		}
	}
}

bool FParameterizeMeshOp::ComputeUVs(FDynamicMesh3& Mesh,  TFunction<bool(float)>& Interrupter, const bool bUsePolygroups, float GlobalScale)
{
	// the UVAltas code is unhappy if you feed it a single degenerate triangle
	bool bNonDegenerate = true;
	if (Mesh.TriangleCount() == 1)
	{
		for (auto TriID : Mesh.TriangleIndicesItr())
		{
			double Area = Mesh.GetTriArea(TriID);
			bNonDegenerate = bNonDegenerate && FMath::Abs(Area) > 1.e-5;
		}
	}

	if (!bNonDegenerate)
	{
		return false;
	}

	// Convert to a dense form.
	FLinearMesh LinearMesh(Mesh, bUsePolygroups);

	// Data to be populated by the UV generation tool
	TArray<FVector2D> UVVertexBuffer;
	TArray<int32>     UVIndexBuffer;
	TArray<int32>     VertexRemapArray; // This maps the UV vertices to the original position vertices.  Note multiple UV vertices might share the same positional vertex (due to UV boundaries)


	float MaxStretch     = Stretch;
	int32 MaxChartNumber = NumCharts;

	TUniquePtr<IProxyLODParameterization> ParameterizationTool = IProxyLODParameterization::CreateTool();
	bool bSuccess = ParameterizationTool->GenerateUVs(Width, Height, Gutter, LinearMesh.VertexBuffer, LinearMesh.IndexBuffer, LinearMesh.AdjacencyBuffer, Interrupter, UVVertexBuffer, UVIndexBuffer, VertexRemapArray, MaxStretch, MaxChartNumber);


	// Add the UVs to the FDynamicMesh
	if (bSuccess)
	{

		const bool bHasAttributes = Mesh.HasAttributes();

		if (bHasAttributes)
		{
			FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();
			Attributes->PrimaryUV()->ClearElements(); // delete existing UVs
		}
		else
		{
			// Add attrs for UVS
			Mesh.EnableAttributes();
		}

		FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();

		// This mesh shouldn't already have UVs.

		checkSlow(UVOverlay->ElementCount() == 0);

		// Add the UVs to the overlay
		int32 NumUVs = UVVertexBuffer.Num();
		TArray<int32> UVOffsetToElID;  UVOffsetToElID.Reserve(NumUVs);

		for (int32 i = 0; i < NumUVs; ++i)
		{
			const FVector2D UV = UVVertexBuffer[i];

			// The associated VertID in the dynamic mesh
			const int32 VertOffset = VertexRemapArray[i];

			// add the UV to the mesh overlay
			const int32 NewID = UVOverlay->AppendElement(FVector2f(UV));
			UVOffsetToElID.Add(NewID);
		}

		int32 NumUVTris = UVIndexBuffer.Num() / 3;
		for (int32 i = 0; i < NumUVTris; ++i)
		{
			int32 t = i * 3;
			// The triangle in UV space
			FIndex3i UVTri(UVIndexBuffer[t], UVIndexBuffer[t + 1], UVIndexBuffer[t + 2]);

			// the triangle in terms of the VertIDs in the DynamicMesh
			FIndex3i TriVertIDs;
			for (int c = 0; c < 3; ++c)
			{
				// the offset for this vertex in the LinearMesh
				int32 Offset = VertexRemapArray[UVTri[c]];

				int32 VertID = LinearMesh.VertToID[Offset];

				TriVertIDs[c] = VertID;
			}

			// NB: this could be slow.. 
			int32 TriID = Mesh.FindTriangle(TriVertIDs[0], TriVertIDs[1], TriVertIDs[2]);

			checkSlow(TriID != FDynamicMesh3::InvalidID);

			FIndex3i ElTri(UVOffsetToElID[ UVTri[0]], UVOffsetToElID[UVTri[1]], UVOffsetToElID[UVTri[2]]);

			// add the triangle to the overlay
			UVOverlay->SetTriangle(TriID, ElTri);
		}

		//if (bNormalizeAreas)
		//{
		//	NormalizeUVAreas(&Mesh, UVOverlay, GlobalScale);
		//}
	}


	return bSuccess;
}








bool FParameterizeMeshOp::ComputeUVs_ExpMap(FDynamicMesh3& Mesh, TFunction<bool(float)>& Interrupter, float GlobalScale)
{
	FMeshNormals::QuickComputeVertexNormals(Mesh);

	FDynamicMeshAABBTree3 AABBTree(&Mesh, true);
	double NearDistSqr;
	int32 SeedTriangleID = AABBTree.FindNearestTriangle(Mesh.GetBounds().Center(), NearDistSqr);
	FFrame3d SeedFrame = Mesh.GetTriFrame(SeedTriangleID);
	FIndex3i SeedNbrs = Mesh.GetTriangle(SeedTriangleID);

	// try to generate consistent frame alignment...
	SeedFrame.ConstrainedAlignPerpAxes(0, 1, 2, FVector3d::UnitX(), FVector3d::UnitY(), 0.95 );

	TMeshLocalParam<FDynamicMesh3> Param(&Mesh);
	//Param.ParamMode = ELocalParamTypes::PlanarProjection;
	//Param.ParamMode = ELocalParamTypes::ExponentialMap;
	Param.ParamMode = ELocalParamTypes::ExponentialMapUpwindAvg;
	Param.ComputeToMaxDistance(FFrame3d(SeedFrame), SeedNbrs, TNumericLimits<float>::Max());

	Mesh.EnableAttributes();
	FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();
	FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
	UVOverlay->ClearElements(); // delete existing UVs
	checkSlow(UVOverlay->ElementCount() == 0);

	FAxisAlignedBox2d Bounds = Param.GetUVBounds();
	FVector2d Center = Bounds.Center();

	for (int32 VertexID : Mesh.VertexIndicesItr())
	{
		check(Param.HasUV(VertexID));
		FVector2d UV = Param.GetUV(VertexID) - Center;
		int32 NewElemID = UVOverlay->AppendElement(FVector2f(UV));
		check(NewElemID == VertexID);
	}

	for (int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		FIndex3i Tri = Mesh.GetTriangle(TriangleID);
		UVOverlay->SetTriangle(TriangleID, Tri);
	}

	return true;
}






void FParameterizeMeshOp::NormalizeUVAreas(const FDynamicMesh3& Mesh, FDynamicMeshUVOverlay* Overlay, float GlobalScale)
{
	FMeshConnectedComponents UVComponents(&Mesh);
	UVComponents.FindConnectedTriangles([&](int32 Triangle0, int32 Triangle1) {
		return Overlay->AreTrianglesConnected(Triangle0, Triangle1);
	});

	// TODO ParallelFor
	for (FMeshConnectedComponents::FComponent& Component : UVComponents)
	{
		const TArray<int>& Triangles = Component.Indices;
		TSet<int> UVElements;
		UVElements.Reserve(Triangles.Num() * 3);
		double AreaUV = 0;
		double Area3D = 0;
		FVector3d Triangle3D[3];
		FVector2d TriangleUV[3];
		FAxisAlignedBox2d BoundsUV = FAxisAlignedBox2d::Empty();

		for (int tid : Triangles)
		{
			FIndex3i TriElements = Overlay->GetTriangle(tid);
			if (!TriElements.Contains(FDynamicMesh3::InvalidID))
			{
				for (int j = 0; j < 3; ++j)
				{
					TriangleUV[j] = FVector2d(Overlay->GetElement(TriElements[j]));
					BoundsUV.Contain(TriangleUV[j]);
					Triangle3D[j] = Mesh.GetVertex(Overlay->GetParentVertex(TriElements[j]));
					UVElements.Add(TriElements[j]);
				}
				AreaUV += VectorUtil::Area(TriangleUV[0], TriangleUV[1], TriangleUV[2]);
				Area3D += VectorUtil::Area(Triangle3D[0], Triangle3D[1], Triangle3D[2]);
			}
		}
		
		double LinearScale = (AreaUV > 0.00001) ? ( FMathd::Sqrt(Area3D) / FMathd::Sqrt(AreaUV)) : 1.0;
		LinearScale = LinearScale * GlobalScale;
		FVector2d ComponentOrigin = BoundsUV.Center();

		for (int elemid : UVElements)
		{
			FVector2d UV = FVector2d(Overlay->GetElement(elemid));
			UV = (UV - ComponentOrigin) * LinearScale + ComponentOrigin;
			Overlay->SetElement(elemid, FVector2f(UV));
		}
	}
}


void FParameterizeMeshOp::CalculateResult(FProgressCancel* Progress)
{

	if (!InputMesh.IsValid())
	{
		return;
	}
	// Need access to the source mesh:

	const FMeshDescription* MeshDescription = InputMesh.Get();

	// Convert to FDynamic Mesh
	{
		FMeshDescriptionToDynamicMesh Converter;
		Converter.bPrintDebugMessages = true;
		ResultMesh->Clear();
		Converter.Convert(MeshDescription, *ResultMesh);
	}

	if (Progress->Cancelled())
	{
		return;
	}

	FDynamicMesh3* BaseMesh = ResultMesh.Get();

	// We can either split the mesh into multiple meshes - one for each poly group and create UVs for each with calls to UVAtlas (potentially in parallel)
	// or we can update the adjacency and use a single call to UVAtlas.
	//
	// We might disable the Split because running our LayoutUV tool on the results it could mangle some triangles, while the single call
	// to UVAtlas automatically does some layout.  This mangling happens some UVtriangles have opposite winding order from the rest of the group and they
	// get put in their own chart..  I noticed that only when I remesh with "prevent normal flipping" I get these inverted uv triangles..
	const bool bSplitIntoMultiple = true;

	// The UV atlas callback uses a float progress-based interrupter. 
	TFunction<bool(float)> Iterrupter = [Progress](float)->bool {return !Progress->Cancelled(); };

	if (UnwrapType == EParamOpUnwrapType::MinStretch && IslandMode == EParamOpIslandMode::Auto)
	{
		// Single call to UVAtlas - perhaps respecting the poly groups.
		bool bSuccess = ComputeUVs(*ResultMesh, Iterrupter, false, AreaScaling);
		return;
	}


	// Count the number of components for which we successufully generate UVs
	int32 SuccessCount = 0;

	// Predicate that reports true if two triangles are in the same group.
	auto GroupPredicate = [this](int32 CurTri, int32 NbrTri)->bool
	{
		const int CurTriGroup = this->ResultMesh->GetTriangleGroup(CurTri);
		const int NbrTriGroup = this->ResultMesh->GetTriangleGroup(NbrTri);
			
		return (CurTriGroup == NbrTriGroup);
	};

	// find group-connected-components
	FMeshConnectedComponents ConnectedComponents(ResultMesh.Get());
	ConnectedComponents.FindConnectedTriangles(GroupPredicate); 

	// Create an array of meshes - one for each poly group
	// NB TIndirectArray automatically deletes the meshes when it goes out of scope.
	TIndirectArray<FDynamicSubmesh3> SubmeshArray;
	for ( FMeshConnectedComponents::FComponent& Component : ConnectedComponents)
	{
		FDynamicSubmesh3* ComponentSubmesh = new FDynamicSubmesh3(BaseMesh, Component.Indices, (int)EMeshComponents::None, false);
		SubmeshArray.Add(ComponentSubmesh);
	}

	// Compute UVs for each component mesh.  NB: this could be parallelized over each
	for (FDynamicSubmesh3& ComponentSubmesh : SubmeshArray)
	{
		FDynamicMesh3& ComponentMesh = ComponentSubmesh.GetSubmesh();
		if (ComponentMesh.TriangleCount() > 0)
		{
			bool bComputedUVs = (UnwrapType == EParamOpUnwrapType::ExpMap) ? 
				ComputeUVs_ExpMap(ComponentMesh, Iterrupter, AreaScaling) : 
				ComputeUVs(ComponentMesh, Iterrupter, false, AreaScaling);

			if (bComputedUVs) SuccessCount++;
		}

		if (Progress->Cancelled())
		{
			return;
		}
	}

	// transfer the UVs back to the ResultMesh
	if (SuccessCount > 0)
	{
		if (ResultMesh->HasAttributes() == false)
		{
			ResultMesh->EnableAttributes();
		}
		FDynamicMeshUVOverlay* UVOverlay = ResultMesh->Attributes()->PrimaryUV();
		UVOverlay->ClearElements(); // delete existing UVs

		for (FDynamicSubmesh3& ComponentSubmesh : SubmeshArray)
		{
			FDynamicMesh3& ComponentMesh = ComponentSubmesh.GetSubmesh();
			if (ComponentMesh.HasAttributes() == false)
			{
				continue;
			}

			// copy the elements
			const FDynamicMeshUVOverlay* ComponentUVOverlay = ComponentMesh.Attributes()->PrimaryUV();
			TArray<int32> ElemIDMap;
			ElemIDMap.SetNum(ComponentUVOverlay->MaxElementID());
			for (int32 ElemID : ComponentUVOverlay->ElementIndicesItr())
			{
				int ParentVertexID = ComponentUVOverlay->GetParentVertex(ElemID);
				int32 BaseElemID = UVOverlay->AppendElement(ComponentUVOverlay->GetElement(ElemID));
				ElemIDMap[ElemID] = BaseElemID;
			}

			// set the triangle values to the new elements
			for (int32 TriangleID : ComponentMesh.TriangleIndicesItr())
			{
				int32 BaseTriangleID = ComponentSubmesh.MapTriangleToBaseMesh(TriangleID);
				FIndex3i SubUVTriangle = ComponentUVOverlay->GetTriangle(TriangleID);
				if (SubUVTriangle.A >= 0 && SubUVTriangle.B >= 0 && SubUVTriangle.C >= 0)
				{
					FIndex3i BaseUVTriangle(ElemIDMap[SubUVTriangle.A], ElemIDMap[SubUVTriangle.B], ElemIDMap[SubUVTriangle.C]);
					UVOverlay->SetTriangle(BaseTriangleID, BaseUVTriangle);
				}
			}

			if (Progress->Cancelled())
			{
				return;
			}
		}

		if (bNormalizeAreas)
		{
			NormalizeUVAreas(*ResultMesh, UVOverlay, AreaScaling);
		}
	}

}