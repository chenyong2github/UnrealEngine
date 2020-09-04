// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeMesh.h"

#include "DatasmithFacadeScene.h"

// Datasmith SDK.
#include "DatasmithMesh.h"
#include "DatasmithMeshExporter.h"


FDatasmithFacadeMesh::FDatasmithFacadeMesh(
	const TCHAR* InElementName
)
	: FDatasmithFacadeElement(FDatasmithSceneFactory::CreateMesh(InElementName))
{}

void FDatasmithFacadeMesh::AddVertex(
	float InX,
	float InY,
	float InZ
)
{
	VertexPointArray.Add(ConvertPosition(InX, InY, InZ));
}

void FDatasmithFacadeMesh::AddUV(
	int   InChannel, 
	float InU,
	float InV
)
{
	if (InChannel >= VertexUVChannelArray.Num())
	{
		VertexUVChannelArray.AddDefaulted(InChannel + 1 - VertexUVChannelArray.Num());
	}

	VertexUVChannelArray[InChannel].Add(FVector2D(InU, InV));
}

void FDatasmithFacadeMesh::AddTriangle(
	int InVertex1,
	int InVertex2,
	int InVertex3,
	int InMaterialID
)
{
	TriangleArray.Add({ InVertex1, InVertex2, InVertex3, InMaterialID });
}

void FDatasmithFacadeMesh::AddTriangle(
	int          InVertex1,
	int          InVertex2,
	int          InVertex3,
	const TCHAR* InMaterialName
)
{
	FSetElementId SetElementId = MaterialNameSet.Add(FString(InMaterialName));

	AddTriangle(InVertex1, InVertex2, InVertex3, SetElementId.AsInteger());

	AddMaterial(SetElementId.AsInteger(), InMaterialName);
}

void FDatasmithFacadeMesh::AddNormal(
	float InX,
	float InY,
	float InZ
)
{
	TriangleNormalArray.Add(ConvertDirection(InX, InY, InZ));
}

void FDatasmithFacadeMesh::AddMaterial(
	int          InMaterialId,
	const TCHAR* InMaterialName
)
{
	if (!MaterialNameMap.Contains(InMaterialId))
	{
		MaterialNameSet.Add(FString(InMaterialName));

		MaterialNameMap.Add(InMaterialId, FString(InMaterialName));
	}
}

int FDatasmithFacadeMesh::GetVertexCount() const
{
	return VertexPointArray.Num();
}

int FDatasmithFacadeMesh::GetTriangleCount() const
{
	return TriangleArray.Num();
}

TSharedPtr<FDatasmithMesh> FDatasmithFacadeMesh::GenerateDatasmithMesh()
{
	// Create a new Datasmith static mesh.
	TSharedPtr<FDatasmithMesh> MeshPtr = MakeShared<FDatasmithMesh>();

	// Get the number of mesh vertices (must be > 0).
	int32 VertexCount = VertexPointArray.Num();

	// Set the number of vertices of the Datasmith static mesh.
	MeshPtr->SetVerticesCount(VertexCount);

	// Set the vertex point in the Datasmith static mesh.
	for (int32 VertexNo = 0; VertexNo < VertexCount; VertexNo++)
	{
		FVector const& VertexPoint = VertexPointArray[VertexNo];
		MeshPtr->SetVertex(VertexNo, VertexPoint.X, VertexPoint.Y, VertexPoint.Z);
	}

	// Get the number of UV channels.
	int32 UVChannelCount = VertexUVChannelArray.Num();

	if (UVChannelCount == 0)
	{
		// A Datasmith static mesh needs at least one UV channel.
		UVChannelCount = 1;

		// Set the number of Datasmith static mesh UV channels.
		MeshPtr->SetUVChannelsCount(UVChannelCount);

		// Set the number of UV texture coordinates in the Datasmith static mesh UV channel.
		MeshPtr->SetUVCount(0, VertexCount);
	}
	else
	{
		// Set the number of Datasmith static mesh UV channels.
		MeshPtr->SetUVChannelsCount(UVChannelCount);

		for (int32 UVChannelNo = 0; UVChannelNo < UVChannelCount; UVChannelNo++)
		{
			TArray<FVector2D> const& VertexUVArray = VertexUVChannelArray[UVChannelNo];

			// Get the number of UV texture coordinates.
			int32 UVCount = VertexUVArray.Num();

			// Set the number of UV texture coordinates in the Datasmith static mesh UV channel.
			MeshPtr->SetUVCount(UVChannelNo, UVCount);

			// Set the UV texture coordinates of the Datasmith static mesh UV channel.
			for (int32 UVNo = 0; UVNo < UVCount; UVNo++)
			{
				FVector2D const& VertexUV = VertexUVArray[UVNo];
				MeshPtr->SetUV(UVChannelNo, UVNo, VertexUV.X, VertexUV.Y);
			}
		}
	}

	// Get the number of mesh triangles (must be > 0).
	int32 TriangleCount = TriangleArray.Num();

	// Set the number of triangles of the Datasmith static mesh.
	MeshPtr->SetFacesCount(TriangleCount);

	for (int32 TriangleNo = 0; TriangleNo < TriangleCount; TriangleNo++)
	{
		// Set the triangle smoothing mask in the Datasmith static mesh.
		uint32 SmoothingMask = 0; // no smoothing
		MeshPtr->SetFaceSmoothingMask(TriangleNo, SmoothingMask);

		// Set the triangle vertex indices and material ID in the Datasmith static mesh.
		MeshTriangle const& Triangle = TriangleArray[TriangleNo];
		MeshPtr->SetFace(TriangleNo, Triangle.Vertex1, Triangle.Vertex2, Triangle.Vertex3, Triangle.MaterialID);

		// Set the triangle channel UV texture coordinates in the Datasmith static mesh.
		for (int32 UVChannelNo = 0; UVChannelNo < UVChannelCount; UVChannelNo++)
		{
			MeshPtr->SetFaceUV(TriangleNo, UVChannelNo, Triangle.Vertex1, Triangle.Vertex2, Triangle.Vertex3);
		}
	}

	// Set the triangle vertex normals in the Datasmith static mesh.
	for (int32 NormalNo = 0; NormalNo < TriangleNormalArray.Num(); NormalNo++)
	{
		FVector const& TriangleNormal = TriangleNormalArray[NormalNo];
		MeshPtr->SetNormal(NormalNo, TriangleNormal.X, TriangleNormal.Y, TriangleNormal.Z);
	}

	return MeshPtr;
}

void FDatasmithFacadeMesh::ExportAsset(
	FString const& InAssetFolder
)
{
	// Build the Datasmith static mesh asset.
	TSharedPtr<FDatasmithMesh> MeshPtr = GenerateDatasmithMesh();

	// Export the Datasmith static mesh asset into an Datasmith mesh file.
	TSharedPtr<IDatasmithMeshElement> MeshElement = GetDatasmithMeshElement();
	FDatasmithMeshExporter MeshExporter;

	if (!MeshExporter.ExportToUObject( MeshElement, *InAssetFolder, *MeshPtr.Get(), nullptr, FDatasmithExportOptions::LightmapUV ) )
	{
		// TODO: Append a message to the build summary.
		FString Msg = FString::Printf(TEXT("WARNING: Cannot export mesh %ls (%ls): %ls"), MeshElement->GetName(), MeshElement->GetLabel(), *MeshExporter.GetLastError());
	}
}

void FDatasmithFacadeMesh::BuildScene(
	FDatasmithFacadeScene& SceneRef
)
{
	TSharedPtr<IDatasmithMeshElement> MeshElement = GetDatasmithMeshElement();

	// Add the material names utilized by the Datasmith static mesh.
	for (auto const& MaterialNameEntry : MaterialNameMap)
	{
		MeshElement->SetMaterial(*MaterialNameEntry.Value, MaterialNameEntry.Key);
	}

	// Add the Datasmith mesh element to the Datasmith scene.
	SceneRef.GetScene()->AddMesh(MeshElement);
}

TSharedRef<IDatasmithMeshElement> FDatasmithFacadeMesh::GetDatasmithMeshElement() const
{
	return StaticCastSharedRef<IDatasmithMeshElement>(InternalDatasmithElement);
}
