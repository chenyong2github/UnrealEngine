// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsDatas.h"
#include "UObject/ReleaseObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

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
	Ar << Vertex.PackedRadiusAndType;
	Ar << Vertex.UCoord;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FPackedHairAttributeVertex& Vertex)
{
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	Ar << Vertex.RootU;
	Ar << Vertex.RootV;
	Ar << Vertex.NormalizedLength;
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

void FHairStrandsInterpolationBulkData::Serialize(FArchive& Ar)
{
	static_assert(sizeof(FHairInterpolation0Vertex::BulkType) == sizeof(FHairInterpolation0Vertex));
	static_assert(sizeof(FHairInterpolation1Vertex::BulkType) == sizeof(FHairInterpolation1Vertex));
	static_assert(sizeof(FHairStrandsRootIndexFormat::BulkType) == sizeof(FHairStrandsRootIndexFormat::Type));

	Interpolation0.BulkSerialize(Ar);
	Interpolation1.BulkSerialize(Ar);
	SimRootPointIndex.BulkSerialize(Ar);
}

void FHairStrandsBulkData::Serialize(FArchive& Ar)
{
	static_assert(sizeof(FHairStrandsPositionFormat::BulkType) == sizeof(FHairStrandsPositionFormat::Type));
	static_assert(sizeof(FHairStrandsAttributeFormat::BulkType) == sizeof(FHairStrandsAttributeFormat::Type));
	static_assert(sizeof(FHairStrandsMaterialFormat::BulkType) == sizeof(FHairStrandsMaterialFormat::Type));
	static_assert(sizeof(FHairStrandsRootIndexFormat::BulkType) == sizeof(FHairStrandsRootIndexFormat::Type)); 

	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);

	Positions.BulkSerialize(Ar);
	Attributes.BulkSerialize(Ar);
	Materials.BulkSerialize(Ar);
	CurveOffsets.BulkSerialize(Ar);

	Ar << CurveCount;
	Ar << PointCount;
	Ar << MaxLength;
	Ar << MaxRadius;
	Ar << BoundingBox;

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
