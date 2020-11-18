// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairCardsDatas.h"

// Serialization code for cards structures
// Note that if there are changes in the serialized structures,
// including the types used in them such as the RenderData structures,
// CustomVersion will be required to handle the changes

FArchive& operator<<(FArchive& Ar, FUIntPoint& Point)
{
	Ar << Point.X;
	Ar << Point.Y;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairCardsGeometry& CardGeometry)
{
	Ar << CardGeometry.UVs;
	Ar << CardGeometry.Normals;
	Ar << CardGeometry.Tangents;
	Ar << CardGeometry.Positions;
	Ar << CardGeometry.Indices;

	Ar << CardGeometry.PointOffsets; // No longer used, kept only for backward compatibility
	Ar << CardGeometry.PointCounts;  // No longer used, kept only for backward compatibility

	Ar << CardGeometry.IndexOffsets;
	Ar << CardGeometry.IndexCounts;

	// Bounds should be serialized
	if (Ar.IsLoading())
	{
		CardGeometry.BoundingBox.Init();
		for (const FVector& P : CardGeometry.Positions)
		{
			CardGeometry.BoundingBox += P;
		}
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairCardsDatas::FRenderData& CardRenderData)
{
	Ar << CardRenderData.Positions;
	Ar << CardRenderData.Normals;
	Ar << CardRenderData.UVs;
	Ar << CardRenderData.Indices;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairCardsDatas& CardData)
{
	Ar << CardData.Cards;
	Ar << CardData.RenderData;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairCardsInterpolationVertex& CardInterpVertex)
{
	if (Ar.IsLoading())
	{
		uint32 Value;
		Ar << Value;
		CardInterpVertex.VertexIndex = Value & 0x00FFFFFF; // first 24 bits
		CardInterpVertex.VertexLerp = Value >> 24;
	}
	else
	{
		uint32 Value = CardInterpVertex.VertexIndex | (CardInterpVertex.VertexLerp << 24);
		Ar << Value;
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairMeshes& HairMesh)
{
	Ar << HairMesh.UVs;
	Ar << HairMesh.Normals;
	Ar << HairMesh.Tangents;
	Ar << HairMesh.Positions;
	Ar << HairMesh.Indices;

	// Bounds should be serialized
	if (Ar.IsLoading())
	{
		HairMesh.BoundingBox.Init();
		for (const FVector& P : HairMesh.Positions)
		{
			HairMesh.BoundingBox += P;
		}
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairMeshesDatas::FRenderData& MeshRenderData)
{
	Ar << MeshRenderData.Positions;
	Ar << MeshRenderData.Normals;
	Ar << MeshRenderData.UVs;
	Ar << MeshRenderData.Indices;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairMeshesDatas& MeshData)
{
	Ar << MeshData.Meshes;
	Ar << MeshData.RenderData;

	return Ar;
}
