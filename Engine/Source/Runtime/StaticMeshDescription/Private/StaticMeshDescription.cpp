// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"


void UStaticMeshDescription::RegisterAttributes()
{
	RequiredAttributes = MakeUnique<FStaticMeshAttributes>(MeshDescription);
	RequiredAttributes->Register();
}


FVector2D UStaticMeshDescription::GetVertexInstanceUV(FVertexInstanceID VertexInstanceID, int32 UVIndex) const
{
	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexInstanceUV: VertexInstanceID %d doesn't exist."), VertexInstanceID.GetValue());
		return FVector2D::ZeroVector;
	}

	if (!MeshDescription.VertexInstanceAttributes().HasAttribute(MeshAttribute::VertexInstance::TextureCoordinate))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexInstanceUV: VertexInstanceAttribute TextureCoordinate doesn't exist."));
		return FVector2D::ZeroVector;
	}

	return MeshDescription.VertexInstanceAttributes().GetAttribute<FVector2D>(VertexInstanceID, MeshAttribute::VertexInstance::TextureCoordinate, UVIndex);
}


void UStaticMeshDescription::SetVertexInstanceUV(FVertexInstanceID VertexInstanceID, FVector2D UV, int32 UVIndex)
{
	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetVertexInstanceUV: VertexInstanceID %d doesn't exist."), VertexInstanceID.GetValue());
		return;
	}

	if (!MeshDescription.VertexInstanceAttributes().HasAttribute(MeshAttribute::VertexInstance::TextureCoordinate))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetVertexInstanceUV: VertexInstanceAttribute TextureCoordinate doesn't exist."));
		return;
	}

	MeshDescription.VertexInstanceAttributes().SetAttribute(VertexInstanceID, MeshAttribute::VertexInstance::TextureCoordinate, UVIndex, UV);
}


void UStaticMeshDescription::SetPolygonGroupMaterialSlotName(FPolygonGroupID PolygonGroupID, const FName& SlotName)
{
	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetPolygonGroupMaterialSlotName: PolygonGroupID %d doesn't exist."), PolygonGroupID.GetValue());
		return;
	}

	if (!MeshDescription.PolygonGroupAttributes().HasAttribute(MeshAttribute::PolygonGroup::ImportedMaterialSlotName))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetPolygonGroupMaterialSlotName: PolygonGroupAttribute ImportedMaterialSlotName doesn't exist."));
		return;
	}

	MeshDescription.PolygonGroupAttributes().SetAttribute(PolygonGroupID, MeshAttribute::PolygonGroup::ImportedMaterialSlotName, 0, SlotName);
}


void UStaticMeshDescription::CreateCube(FVector Center, FVector HalfExtents, FPolygonGroupID PolygonGroup,
										FPolygonID& PolygonID_PlusX,
										FPolygonID& PolygonID_MinusX,
										FPolygonID& PolygonID_PlusY, 
										FPolygonID& PolygonID_MinusY,
										FPolygonID& PolygonID_PlusZ,
										FPolygonID& PolygonID_MinusZ)
{
	TVertexAttributesRef<FVector> Positions = GetVertexPositions();

	FVertexID VertexIDs[8];

	MeshDescription.ReserveNewVertices(8);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		VertexIDs[Index] = MeshDescription.CreateVertex();
	}

	Positions[VertexIDs[0]] = Center + HalfExtents * FVector( 1.0f, -1.0f,  1.0f);
	Positions[VertexIDs[1]] = Center + HalfExtents * FVector( 1.0f,  1.0f,  1.0f);
	Positions[VertexIDs[2]] = Center + HalfExtents * FVector(-1.0f,  1.0f,  1.0f);
	Positions[VertexIDs[3]] = Center + HalfExtents * FVector(-1.0f, -1.0f,  1.0f);
	Positions[VertexIDs[4]] = Center + HalfExtents * FVector(-1.0f,  1.0f, -1.0f);
	Positions[VertexIDs[5]] = Center + HalfExtents * FVector(-1.0f, -1.0f, -1.0f);
	Positions[VertexIDs[6]] = Center + HalfExtents * FVector( 1.0f, -1.0f, -1.0f);
	Positions[VertexIDs[7]] = Center + HalfExtents * FVector( 1.0f,  1.0f, -1.0f);

	auto MakePolygon = [this, &VertexIDs, PolygonGroup](int32 P0, int32 P1, int32 P2, int32 P3) -> FPolygonID
	{
		FVertexInstanceID VertexInstanceIDs[4];
		VertexInstanceIDs[0] = MeshDescription.CreateVertexInstance(VertexIDs[P0]);
		VertexInstanceIDs[1] = MeshDescription.CreateVertexInstance(VertexIDs[P1]);
		VertexInstanceIDs[2] = MeshDescription.CreateVertexInstance(VertexIDs[P2]);
		VertexInstanceIDs[3] = MeshDescription.CreateVertexInstance(VertexIDs[P3]);

		TArray<FEdgeID> EdgeIDs;
		EdgeIDs.Reserve(4);

		FPolygonID PolygonID = MeshDescription.CreatePolygon(PolygonGroup, VertexInstanceIDs, &EdgeIDs);

		for (FEdgeID EdgeID : EdgeIDs)
		{
			GetEdgeHardnesses()[EdgeID] = true;
		}

		return PolygonID;
	};

	PolygonID_PlusX = MakePolygon(0, 1, 7, 6);
	PolygonID_MinusX = MakePolygon(2, 3, 5, 4);
	PolygonID_PlusY = MakePolygon(1, 2, 4, 7);
	PolygonID_MinusY = MakePolygon(3, 0, 6, 5);
	PolygonID_PlusZ = MakePolygon(1, 0, 3, 2);
	PolygonID_MinusZ = MakePolygon(5, 4, 7, 6);

	MeshDescription.PolygonAttributes().RegisterAttribute<FVector>(MeshAttribute::Polygon::Normal, 1, FVector::ZeroVector, EMeshAttributeFlags::Transient);
	MeshDescription.PolygonAttributes().RegisterAttribute<FVector>(MeshAttribute::Polygon::Tangent, 1, FVector::ZeroVector, EMeshAttributeFlags::Transient);
	MeshDescription.PolygonAttributes().RegisterAttribute<FVector>(MeshAttribute::Polygon::Binormal, 1, FVector::ZeroVector, EMeshAttributeFlags::Transient);
	MeshDescription.PolygonAttributes().RegisterAttribute<FVector>(MeshAttribute::Polygon::Center, 1, FVector::ZeroVector, EMeshAttributeFlags::Transient);

	FStaticMeshOperations::ComputePolygonTangentsAndNormals(MeshDescription);
	FStaticMeshOperations::ComputeTangentsAndNormals(MeshDescription, EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents);
}


