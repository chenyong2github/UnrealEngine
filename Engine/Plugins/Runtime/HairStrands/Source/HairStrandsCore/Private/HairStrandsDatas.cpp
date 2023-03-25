// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsDatas.h"
#include "UObject/ReleaseObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"
#include "HairAttributes.h"

void FHairStrandsInterpolationDatas::SetNum(const uint32 NumCurves)
{
	PointsSimCurvesVertexWeights.SetNum(NumCurves);
	PointsSimCurvesVertexLerp.SetNum(NumCurves);
	PointsSimCurvesVertexIndex.SetNum(NumCurves);
	PointsSimCurvesIndex.SetNum(NumCurves);
}

void FHairStrandsCurves::SetNum(const uint32 NumCurves, uint32 InAttributes)
{
	CurvesOffset.SetNum(NumCurves + 1);
	CurvesCount.SetNum(NumCurves);
	CurvesLength.SetNum(NumCurves);

	// Not initialized to track if the data are available
	if (HasHairAttribute(InAttributes, EHairAttribute::RootUV))		{ CurvesRootUV.SetNum(NumCurves); }
	if (HasHairAttribute(InAttributes, EHairAttribute::StrandID))	{ StrandIDs.SetNum(NumCurves); }
	if (HasHairAttribute(InAttributes, EHairAttribute::ClumpID))	{ ClumpIDs.SetNum(NumCurves); }
	if (HasHairAttribute(InAttributes, EHairAttribute::PrecomputedGuideWeights))
	{
		CurvesClosestGuideIDs.SetNum(NumCurves);
		CurvesClosestGuideWeights.SetNum(NumCurves);
	}
}

void FHairStrandsPoints::SetNum(const uint32 NumPoints, uint32 InAttributes)
{
	PointsPosition.SetNum(NumPoints);
	PointsRadius.SetNum(NumPoints);
	PointsCoordU.SetNum(NumPoints);

	// Not initialized to track if the data are available
	if (HasHairAttribute(InAttributes, EHairAttribute::Color))		{ PointsBaseColor.SetNum(NumPoints); }
	if (HasHairAttribute(InAttributes, EHairAttribute::Roughness))	{ PointsRoughness.SetNum(NumPoints); }
	if (HasHairAttribute(InAttributes, EHairAttribute::AO))			{ PointsAO.SetNum(NumPoints); }
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
	ClumpIDs.Reset();
	CurvesClosestGuideIDs.Reset();
	CurvesClosestGuideWeights.Reset();
}

void FHairStrandsPoints::Reset()
{
	PointsPosition.Reset();
	PointsRadius.Reset();
	PointsCoordU.Reset();
	PointsBaseColor.Reset();
	PointsRoughness.Reset();
	PointsAO.Reset();
}

float GetHairStrandsMaxLength(const FHairStrandsDatas& In)
{
	float MaxLength = 0;
	for (float CurveLength : In.StrandsCurves.CurvesLength)
	{
		MaxLength = FMath::Max(MaxLength, CurveLength);
	}
	return MaxLength;
}

float GetHairStrandsMaxRadius(const FHairStrandsDatas& In)
{
	float MaxRadius = 0;
	for (float PointRadius : In.StrandsPoints.PointsRadius)
	{
		MaxRadius = FMath::Max(MaxRadius, PointRadius);
	}
	return MaxRadius;
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

void FHairStrandsInterpolationBulkData::Reset()
{
	Header.Flags = 0;
	Header.PointCount = 0;
	Header.SimPointCount = 0;
	
	// Deallocate memory if needed
	Data.Interpolation.RemoveBulkData();
	Data.SimRootPointIndex.RemoveBulkData();

	// Reset the bulk byte buffer to ensure the (serialize) data size is reset to 0
	Data.Interpolation		= FByteBulkData();
	Data.SimRootPointIndex	= FByteBulkData();
}

void FHairStrandsInterpolationBulkData::Serialize(FArchive& Ar, UObject* Owner)
{
	SerializeHeader(Ar, Owner);
	SerializeData(Ar, Owner);
}

void FHairStrandsInterpolationBulkData::SerializeHeader(FArchive& Ar, UObject* Owner)
{
	Ar << Header.Flags;
	Ar << Header.PointCount;
	Ar << Header.SimPointCount;
}

void FHairStrandsInterpolationBulkData::SerializeData(FArchive& Ar, UObject* Owner)
{
	static_assert(sizeof(FHairStrandsRootIndexFormat::BulkType) == sizeof(FHairStrandsRootIndexFormat::Type));

	if (!!(Header.Flags & DataFlags_HasData))
	{
		const int32 ChunkIndex = 0;
		bool bAttemptFileMapping = false;
		Data.Interpolation.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
		Data.SimRootPointIndex.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	}
}

bool FHairStrandsInterpolationBulkData::HasData() const
{
	return Header.Flags & DataFlags_HasData;
}

void FHairStrandsInterpolationBulkData::Request(FBulkDataBatchRequest& InRequest)
{
	if (!(Header.Flags & DataFlags_HasData))
	{
		return;
	}

	check(InRequest.IsNone());
	FBulkDataBatchRequest::FBatchBuilder Batch = FBulkDataBatchRequest::NewBatch(2);
	Batch.Read(Data.Interpolation);
	Batch.Read(Data.SimRootPointIndex);
	Batch.Issue(InRequest);
}

void FHairStrandsBulkData::Serialize(FArchive& Ar, UObject* Owner)
{
	SerializeHeader(Ar, Owner);
	SerializeData(Ar, Owner);
}

void FHairStrandsBulkData::SerializeHeader(FArchive& Ar, UObject* Owner)
{
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);

	Ar << Header.CurveCount;
	Ar << Header.PointCount;
	Ar << Header.MaxLength;
	Ar << Header.MaxRadius;
	Ar << Header.BoundingBox;
	Ar << Header.Flags;
	for (uint8 AttributeIt = 0; AttributeIt < HAIR_CURVE_ATTRIBUTE_COUNT; ++AttributeIt)
	{
		Ar << Header.CurveAttributeOffsets[AttributeIt];
	}
	for (uint8 AttributeIt = 0; AttributeIt < HAIR_POINT_ATTRIBUTE_COUNT; ++AttributeIt)
	{
		Ar << Header.PointAttributeOffsets[AttributeIt];
	}
	Ar << Header.ImportedAttributes;
	Ar << Header.ImportedAttributeFlags;
}


void FHairStrandsBulkData::SerializeData(FArchive& Ar, UObject* Owner)
{
	static_assert(sizeof(FHairStrandsPositionFormat::BulkType) == sizeof(FHairStrandsPositionFormat::Type));
	static_assert(sizeof(FHairStrandsAttributeFormat::BulkType) == sizeof(FHairStrandsAttributeFormat::Type));
	static_assert(sizeof(FHairStrandsPointToCurveFormat16::BulkType) == sizeof(FHairStrandsPointToCurveFormat16::Type));
	static_assert(sizeof(FHairStrandsPointToCurveFormat32::BulkType) == sizeof(FHairStrandsPointToCurveFormat32::Type));
	static_assert(sizeof(FHairStrandsRootIndexFormat::BulkType) == sizeof(FHairStrandsRootIndexFormat::Type)); 

	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	  
	// Forced not inline means the bulk data won't automatically be loaded when we deserialize
	// but only when we explicitly take action to load it
	if (Ar.IsSaving())
	{
		const uint32 BulkFlags = BULKDATA_Force_NOT_InlinePayload;
		Data.Positions.SetBulkDataFlags(BulkFlags);
		Data.CurveAttributes.SetBulkDataFlags(BulkFlags);
		if (Header.Flags & DataFlags_HasPointAttribute)
		{
			Data.PointAttributes.SetBulkDataFlags(BulkFlags);
		}
		Data.PointToCurve.SetBulkDataFlags(BulkFlags);
		Data.Curves.SetBulkDataFlags(BulkFlags);
	}

	if (!!(Header.Flags & DataFlags_HasData))
	{
		const int32 ChunkIndex = 0;
		bool bAttemptFileMapping = false;

		Data.Positions.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
		if (Header.Flags & DataFlags_HasPointAttribute)
		{
			Data.PointAttributes.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
		}
		Data.CurveAttributes.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
		Data.PointToCurve.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
		Data.Curves.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	}
}

void FHairStrandsBulkData::Request(FBulkDataBatchRequest& In)
{
	if (!!(Header.Flags & DataFlags_HasData))
	{
		check(In.IsNone());

		FBulkDataBatchRequest::FBatchBuilder Batch = FBulkDataBatchRequest::NewBatch(5);
		Batch.Read(Data.Positions);
		if (Header.Flags & DataFlags_HasPointAttribute)
		{
			Batch.Read(Data.PointAttributes);
		}
		Batch.Read(Data.CurveAttributes);
		Batch.Read(Data.PointToCurve);
		Batch.Read(Data.Curves);
		Batch.Issue(In);
	}
}

void FHairStrandsBulkData::Reset()
{
	Header.CurveCount = 0;
	Header.PointCount = 0;
	Header.MaxLength = 0;
	Header.MaxRadius = 0;
	Header.BoundingBox = FBox(EForceInit::ForceInit);
	Header.Flags = 0;
	for (uint8 AttributeIt = 0; AttributeIt < HAIR_CURVE_ATTRIBUTE_COUNT; ++AttributeIt)
	{
		Header.CurveAttributeOffsets[AttributeIt] = 0xFFFFFFFF;
	}
	for (uint8 AttributeIt = 0; AttributeIt < HAIR_POINT_ATTRIBUTE_COUNT; ++AttributeIt)
	{
		Header.PointAttributeOffsets[AttributeIt] = 0xFFFFFFFF;
	}
	// Deallocate memory if needed
	Data.Positions.RemoveBulkData();
	Data.CurveAttributes.RemoveBulkData();
	Data.PointAttributes.RemoveBulkData();
	Data.PointToCurve.RemoveBulkData();
	Data.Curves.RemoveBulkData();

	// Reset the bulk byte buffer to ensure the (serialize) data size is reset to 0
	Data.Positions 		= FByteBulkData();
	Data.CurveAttributes= FByteBulkData();
	Data.PointAttributes= FByteBulkData();
	Data.PointToCurve	= FByteBulkData();
	Data.Curves			= FByteBulkData();
}

void FHairStrandsDatas::Reset()
{
	StrandsCurves.Reset();
	StrandsPoints.Reset();
	HairDensity = 1;
	BoundingBox = FBox(EForceInit::ForceInit);
}
