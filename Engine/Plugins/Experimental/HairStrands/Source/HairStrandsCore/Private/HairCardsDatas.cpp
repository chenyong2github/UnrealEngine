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

	Ar << CardGeometry.PointOffsets;
	Ar << CardGeometry.PointCounts;

	Ar << CardGeometry.IndexOffsets;
	Ar << CardGeometry.IndexCounts;

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

FArchive& operator<<(FArchive& Ar, FHairCardsProceduralGeometry::Rect& Rect)
{
	Ar << Rect.Offset;
	Ar << Rect.Resolution;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairOrientedBound& Bound)
{
	Ar << Bound.Center;
	Ar << Bound.ExtentX;
	Ar << Bound.ExtentY;
	Ar << Bound.ExtentZ;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairCardsProceduralGeometry& ProceduralCardGeometry)
{
	Ar << *static_cast<FHairCardsGeometry*>(&ProceduralCardGeometry);
	Ar << ProceduralCardGeometry.CardIndices;
	Ar << ProceduralCardGeometry.Rects;
	Ar << ProceduralCardGeometry.Lengths;
	Ar << ProceduralCardGeometry.Bounds;

	// Does these need to be serialized since its editor only for texture generation?
	//Ar << ProceduralCardGeometry.CardIndexToClusterOffsetAndCount;
	//Ar << ProceduralCardGeometry.ClusterIndexToVertexOffsetAndCount;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairCardsProceduralAtlas::Rect& Rect)
{
	Ar << Rect.Offset;
	Ar << Rect.Resolution;
	Ar << Rect.VertexOffset;
	Ar << Rect.VertexCount;
	Ar << Rect.MinBound;
	Ar << Rect.MaxBound;
	Ar << Rect.RasterAxisX;
	Ar << Rect.RasterAxisY;
	Ar << Rect.RasterAxisZ;
	Ar << Rect.CardWidth;
	Ar << Rect.CardLength;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairCardsProceduralAtlas& Atlas)
{
	Ar << Atlas.Resolution;
	Ar << Atlas.Rects;
	Ar << Atlas.StrandsPositions;

	return Ar;
}

//FArchive& operator<<(FArchive& Ar, FHairCardsVoxel& Voxel)
//{
//	return Ar;
//}

FArchive& operator<<(FArchive& Ar, FHairCardsAtlasRectFormat::Type& AtlasRect)
{
	Ar << AtlasRect.X;
	Ar << AtlasRect.Y;
	Ar << AtlasRect.Z;
	Ar << AtlasRect.W;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairCardsProceduralDatas::FRenderData& RenderData)
{
	Ar << RenderData.Positions;
	Ar << RenderData.Normals;
	Ar << RenderData.UVs;
	Ar << RenderData.Indices;
	Ar << RenderData.CardsRect;
	Ar << RenderData.CardsLengths;
	Ar << RenderData.CardsStrandsPositions;
	Ar << RenderData.CardItToCluster;
	Ar << RenderData.ClusterIdToVertices;
	Ar << RenderData.ClusterBounds;
	Ar << RenderData.VoxelDensity;
	Ar << RenderData.VoxelTangent;
	Ar << RenderData.VoxelNormal;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairCardsProceduralDatas& ProceduralCardData)
{
	ProceduralCardData.Guides.Serialize(Ar);
	Ar << ProceduralCardData.Cards;
	Ar << ProceduralCardData.Atlas;
	//Ar << ProceduralCardData.Voxels; // internal structure for debug purpose, don't need to be serialized?
	Ar << ProceduralCardData.RenderData;

	return Ar;
}
