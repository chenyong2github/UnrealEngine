// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ParameterizationOps/ParameterizeMeshOp.h"

#include "DynamicMeshAttributeSet.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "ProxyLODParameterization.h"




FParameterizeMeshOp::FLinearMesh::FLinearMesh(const FDynamicMesh3& Mesh)
{

	TArray<FVector>& Positions = this->VertexBuffer;

	// Temporary maps used during construction.

	TArray<int32> TriFromID;
	TArray<int32> TriToID;
	TArray<int32> VertFromID;

	// Compute the mapping from triangle ID to triangle number
	{
		const int32 MaxTriID = Mesh.MaxTriangleID();
		const int32 NumTris = Mesh.TriangleCount();

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
		const int32 NumVerts = Mesh.VertexCount();

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

	for (int TriID : Mesh.TriangleIndicesItr())
	{

		// NB: this maybe the same as GetNeighborTriangles()
		for (int i = 0; i < 3; ++i)
		{
			int32 EdgesID = Mesh.GetTriEdge(TriID, i);
			FIndex4i Edge = Mesh.GetEdge(EdgesID);
			int32 OtherTriID = (Edge[2] == TriID) ? Edge[3] : Edge[2];

			int32 RemapOtherID = (OtherTriID != FDynamicMesh3::InvalidID) ? TriFromID[OtherTriID] : FDynamicMesh3::InvalidID;

			AdjacencyBuffer.Add(RemapOtherID);
		}
	}

}

bool FParameterizeMeshOp::ComputeUVs(FDynamicMesh3& Mesh, TFunction<bool(float)>& Interrupter)
{
	// Convert to a dense form.
	FLinearMesh LinearMesh(Mesh);

	// Data to be populated by the UV generation tool
	TArray<FVector2D> UVVertexBuffer;
	TArray<int32>     UVIndexBuffer;
	TArray<int32>     VertexRemapArray;


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
		for (int32 i = 0; i < NumUVs; ++i)
		{
			const FVector2D UV = UVVertexBuffer[i];

			// The associated VertID in the dynamic mesh
			const int32 VertOffset = VertexRemapArray[i];
			const int32 VertID = LinearMesh.VertToID[VertOffset];

			// add the UV to the mesh overlay
			const int32 NewID = UVOverlay->AppendElement(UV, VertID);

		}

		int32 NumUVTris = UVIndexBuffer.Num() / 3;
		for (int32 i = 0; i < NumUVTris; ++i)
		{
			int32 t = i * 3;
			// The triangle in UV space
			FIndex3i TriUV(UVIndexBuffer[t], UVIndexBuffer[t + 1], UVIndexBuffer[t + 2]);

			// the triangle in terms of the VertIDs in the DynamicMesh
			FIndex3i TriVertIDs;
			for (int c = 0; c < 3; ++c)
			{
				// the offset for this vertex in the LinearMesh
				int32 Offset = VertexRemapArray[TriUV[c]];

				int32 VertId = LinearMesh.VertToID[Offset];

				TriVertIDs[c] = VertId;
			}

			// NB: this could be slow.. 
			int32 TriID = Mesh.FindTriangle(TriVertIDs[0], TriVertIDs[1], TriVertIDs[2]);

			checkSlow(TriID != FDynamicMesh3::InvalidID);

			// add the triangle to the overlay
			UVOverlay->SetTriangle(TriID, TriUV);
		}

	}

	return bSuccess;
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

	// The UV atlas callback uses a float progress-based interrupter. 
	TFunction<bool(float)> Iterrupter = [Progress](float)->bool {return !Progress->Cancelled(); };


	bool bSuccess = ComputeUVs(*ResultMesh,  Iterrupter);

}