// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsDatas.h"

void FHairStrandsInterpolationDatas::SetNum(const uint32 NumCurves)
{
	PointsSimCurvesVertexWeights.SetNum(NumCurves);
	PointsSimCurvesVertexIndex.SetNum(NumCurves);
	PointsSimCurvesIndex.SetNum(NumCurves);
}

void FHairStrandsCurves::SetNum(const uint32 NumCurves)
{
	CurvesOffset.SetNum(NumCurves + 1);
	CurvesCount.SetNum(NumCurves);
	CurvesLength.SetNum(NumCurves);
	CurvesRootUV.SetNum(NumCurves);
}

void FHairStrandsPoints::SetNum(const uint32 NumPoints)
{
	PointsPosition.SetNum(NumPoints);
	PointsRadius.SetNum(NumPoints);
	PointsCoordU.SetNum(NumPoints);
}

void FHairStrandsInterpolationDatas::Reset()
{
	PointsSimCurvesVertexWeights.Reset();
	PointsSimCurvesVertexIndex.Reset();
	PointsSimCurvesIndex.Reset();
}

void FHairStrandsCurves::Reset()
{
	CurvesOffset.Reset();
	CurvesCount.Reset();
	CurvesLength.Reset();
	CurvesRootUV.Reset();
}

void FHairStrandsPoints::Reset()
{
	PointsPosition.Reset();
	PointsRadius.Reset();
	PointsCoordU.Reset();
}

FArchive& operator<<(FArchive& Ar, FPackedHairVertex& Vertex)
{
	Ar << Vertex.X;
	Ar << Vertex.Y;
	Ar << Vertex.Z;

	Ar << Vertex.NormalizedLength;

	if (Ar.IsLoading())
	{
		uint8 Value;
		Ar << Value;
		Vertex.ControlPointType = Value & 0x03; // first 2 bits
		Vertex.NormalizedRadius = Value >> 2;
	}
	else
	{
		uint8 Value = Vertex.ControlPointType | (Vertex.NormalizedRadius << 2);
		Ar << Value;
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FPackedHairAttributeVertex& Vertex)
{
	Ar << Vertex.RootU;
	Ar << Vertex.RootV;
	Ar << Vertex.UCoord;
	Ar << Vertex.Seed;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairInterpolation0Vertex& Vertex)
{
	Ar << Vertex.Index0;
	Ar << Vertex.Index1;
	Ar << Vertex.Index2;
	Ar << Vertex.VertexWeight0;
	Ar << Vertex.VertexWeight1;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairInterpolation1Vertex& Vertex)
{
	Ar << Vertex.VertexIndex0;
	Ar << Vertex.VertexIndex1;
	Ar << Vertex.VertexIndex2;
	Ar << Vertex.Pad0;

	return Ar;
}

void FHairStrandsInterpolationDatas::FRenderData::Serialize(FArchive& Ar)
{
	Ar << Interpolation0;
	Ar << Interpolation1;
}

void FHairStrandsInterpolationDatas::Serialize(FArchive& Ar)
{
	Ar << PointsSimCurvesVertexWeights;
	Ar << PointsSimCurvesVertexIndex;
	Ar << PointsSimCurvesIndex;

	RenderData.Serialize(Ar);
}

void FHairStrandsPoints::Serialize(FArchive& Ar)
{
	Ar << PointsPosition;
	Ar << PointsRadius;
	Ar << PointsCoordU;
}

void FHairStrandsCurves::Serialize(FArchive& Ar)
{
	Ar << CurvesCount;
	Ar << CurvesOffset;
	Ar << CurvesLength;
	Ar << CurvesRootUV;
	Ar << MaxLength;
	Ar << MaxRadius;
}

void FHairStrandsDatas::FRenderData::Serialize(FArchive& Ar)
{
	Ar << RenderingPositions;
	Ar << RenderingAttributes;
}

void FHairStrandsDatas::Serialize(FArchive& Ar)
{
	StrandsPoints.Serialize(Ar);
	StrandsCurves.Serialize(Ar);
	Ar << HairDensity;
	Ar << BoundingBox;

	RenderData.Serialize(Ar);
}

void FHairStrandsDatas::Reset()
{
	StrandsCurves.Reset();
	StrandsPoints.Reset();
}
