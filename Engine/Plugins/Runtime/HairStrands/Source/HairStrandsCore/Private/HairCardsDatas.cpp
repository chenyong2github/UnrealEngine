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

void FHairCardsBulkData::Serialize(FArchive& Ar)
{
	Ar << Positions;
	Ar << Normals;
	Ar << UVs;
	Ar << Indices;
}

void FHairCardsDatas::Serialize(FArchive& Ar, FHairCardsBulkData& InBulkData)
{
	Ar << Cards;
	InBulkData.Serialize(Ar);
	if (Ar.IsLoading())
	{
		InBulkData.BoundingBox = Cards.BoundingBox;
	}
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

void FHairMeshesBulkData::Serialize(FArchive& Ar)
{
	Ar << Positions;
	Ar << Normals;
	Ar << UVs;
	Ar << Indices;
	Ar << BoundingBox;
}

