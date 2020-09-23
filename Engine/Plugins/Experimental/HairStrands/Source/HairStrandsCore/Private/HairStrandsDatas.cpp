// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsDatas.h"
#include "UObject/ReleaseObjectVersion.h"

void FHairStrandsInterpolationDatas::SetNum(const uint32 NumCurves)
{
	PointsSimCurvesVertexWeights.SetNum(NumCurves);
	PointsSimCurvesVertexLerp.SetNum(NumCurves);
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
	PointsBaseColor.SetNum(NumPoints);
	PointsRoughness.SetNum(NumPoints);
}

void FHairStrandsInterpolationDatas::Reset()
{
	PointsSimCurvesVertexWeights.Reset();
	PointsSimCurvesVertexLerp.Reset();
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

FArchive& operator<<(FArchive& Ar, FVector4_16& Vertex)
{
	Ar << Vertex.X;
	Ar << Vertex.Y;
	Ar << Vertex.Z;
	Ar << Vertex.W;
	return Ar;
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
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	Ar << Vertex.RootU;
	Ar << Vertex.RootV;
	Ar << Vertex.UCoord;
	Ar << Vertex.Seed;

	if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::GroomAssetVersion1)
	{
		Ar << Vertex.IndexU;
		Ar << Vertex.IndexV;
		Ar << Vertex.Unused0;
		Ar << Vertex.Unused1;
	}
	else
	{
		Vertex.IndexU = 0;
		Vertex.IndexV = 0;
		Vertex.Unused0 = 0;
		Vertex.Unused1 = 0;
	}
	

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairMaterialVertex& Vertex)
{
	Ar << Vertex.BaseColorR;
	Ar << Vertex.BaseColorG;
	Ar << Vertex.BaseColorB;
	Ar << Vertex.Roughness;

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
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::GroomAssetVersion3)
	{
		Ar << Vertex.VertexIndex0;
		Ar << Vertex.VertexIndex1;
		Ar << Vertex.VertexIndex2;

		Ar << Vertex.VertexLerp0;
		Ar << Vertex.VertexLerp1;
		Ar << Vertex.VertexLerp2;

		Ar << Vertex.Pad0;
		Ar << Vertex.Pad1;
	}
	else
	{
		Ar << Vertex.VertexIndex0;
		Ar << Vertex.VertexIndex1;
		Ar << Vertex.VertexIndex2;

		uint8 Pad0 = 0;
		Ar << Pad0;

		if (Ar.IsLoading())
		{
			Vertex.VertexLerp0 = 0;
			Vertex.VertexLerp1 = 0;
			Vertex.VertexLerp2 = 0;
		}
	}

	return Ar;
}

void FHairStrandsInterpolationDatas::FRenderData::Serialize(FArchive& Ar)
{
	Ar << Interpolation0;
	Ar << Interpolation1;
}

void FHairStrandsInterpolationDatas::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	Ar << PointsSimCurvesVertexWeights;
	Ar << PointsSimCurvesVertexIndex;
	Ar << PointsSimCurvesIndex;

	if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::GroomAssetVersion3)
	{
		Ar << PointsSimCurvesVertexLerp;
	}
	else if (Ar.IsLoading())
	{
		const uint32 ElmentCount = PointsSimCurvesVertexIndex.Num();
		PointsSimCurvesVertexLerp.SetNum(ElmentCount);
		for (FVector& S : PointsSimCurvesVertexLerp)
		{
			S = FVector::ZeroVector;
		}
	}

	RenderData.Serialize(Ar);
}

void FHairStrandsPoints::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	Ar << PointsPosition;
	Ar << PointsRadius;
	Ar << PointsCoordU;

	if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::GroomAssetVersion2)
	{
		Ar << PointsBaseColor;
		Ar << PointsRoughness;
	}
	else
	{
		const uint32 ElementCount = PointsPosition.Num();
		PointsBaseColor.InsertZeroed(0, ElementCount);
		PointsRoughness.InsertZeroed(0, ElementCount);
	}
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
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	Ar << Positions;
	Ar << Attributes;
	if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::GroomAssetVersion2)
	{
		Ar << Materials;
	}
	else
	{
		const uint32 ElementCount = Attributes.Num();
		Materials.InsertZeroed(0, ElementCount);
	}
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

FArchive& operator<<(FArchive& Ar, FHairStrandsClusterCullingData::FHairClusterInfo& Info)
{
	Ar << Info.LODCount;
	Ar << Info.LODInfoOffset;
	Ar << Info.ScreenSize;
	Ar << Info.bIsVisible;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairStrandsClusterCullingData::FHairClusterLODInfo& Info)
{
	Ar << Info.VertexOffset;
	Ar << Info.VertexCount0;
	Ar << Info.VertexCount1;
	Ar << Info.RadiusScale0;
	Ar << Info.RadiusScale1;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHairStrandsClusterCullingData::FHairClusterInfo::Packed& Info)
{
	uint32* Uint32Data = (uint32*)&Info;
	Ar << Uint32Data[0];
	Ar << Uint32Data[1];
	Ar << Uint32Data[2];
	Ar << Uint32Data[3];
	return Ar;
}