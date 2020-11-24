// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairDescription.h"
#include "Misc/SecureHash.h"
#include "Serialization/BulkDataReader.h"
#include "Serialization/BulkDataWriter.h"

const FStrandID FStrandID::Invalid(TNumericLimits<uint32>::Max());
const FGroomID FGroomID::Invalid(TNumericLimits<uint32>::Max());

FHairDescription::FHairDescription()
	: NumVertices(0)
	, NumStrands(0)
{
	// Required attributes
	StrandAttributesSet.RegisterAttribute<int>(HairAttribute::Strand::VertexCount);
	VertexAttributesSet.RegisterAttribute<FVector>(HairAttribute::Vertex::Position, 1, FVector::ZeroVector);

	// Only one set of groom attributes
	GroomAttributesSet.Initialize(1);
}

void FHairDescription::InitializeVertices(int32 InNumVertices)
{
	NumVertices = InNumVertices;
	VertexAttributesSet.Initialize(InNumVertices);
}

void FHairDescription::InitializeStrands(int32 InNumStrands)
{
	NumStrands = InNumStrands;
	StrandAttributesSet.Initialize(InNumStrands);
}

FVertexID FHairDescription::AddVertex()
{
	FVertexID VertexID = FVertexID(NumVertices++);
	VertexAttributesSet.Insert(VertexID);
	return VertexID;
}

FStrandID FHairDescription::AddStrand()
{
	FStrandID StrandID = FStrandID(NumStrands++);
	StrandAttributesSet.Insert(StrandID);
	return StrandID;
}

void FHairDescription::Reset()
{
	NumVertices = 0;
	NumStrands = 0;

	VertexAttributesSet.Initialize(0);
	StrandAttributesSet.Initialize(0);
	GroomAttributesSet.Initialize(0);
}

bool FHairDescription::IsValid() const
{
	return (NumStrands > 0) && (NumVertices > 0);
}

void FHairDescription::Serialize(FArchive& Ar)
{
	Ar << NumVertices;
	Ar << NumStrands;

	Ar << VertexAttributesSet;
	Ar << StrandAttributesSet;
	Ar << GroomAttributesSet;
}

#if WITH_EDITORONLY_DATA

void FHairDescriptionBulkData::Serialize(FArchive& Ar, UObject* Owner)
{
	if (Ar.IsLoading())
	{
		// If loading, take the package custom version so it can be applied to the bulk data archive
		// when unpacking HairDescription from it
		CustomVersions = Ar.GetCustomVersions();
	}

	BulkData.Serialize(Ar, Owner);

	Ar << Guid;
}

void FHairDescriptionBulkData::SaveHairDescription(FHairDescription& HairDescription)
{
	BulkData.RemoveBulkData();

	if (HairDescription.IsValid())
	{
		FBulkDataWriter Ar(BulkData, /*bIsPersistent*/ true);
		HairDescription.Serialize(Ar);

		// Preserve CustomVersions at save time so we can reuse the same ones when reloading direct from memory
		CustomVersions = Ar.GetCustomVersions();
	}

	// Use bulk data hash instead of guid to identify content to improve DDC cache hit
	ComputeGuidFromHash();
}


void FHairDescriptionBulkData::LoadHairDescription(FHairDescription& HairDescription)
{
	HairDescription.Reset();

	if (!IsEmpty())
	{
		FBulkDataReader Ar(BulkData, /*bIsPersistent*/ true);

		// Propagate the custom version information from the package to the bulk data, so that the HairDescription
		// is serialized with the same versioning
		Ar.SetCustomVersions(CustomVersions);

		HairDescription.Serialize(Ar);
	}
}

void FHairDescriptionBulkData::Empty()
{
	BulkData.RemoveBulkData();
}

FString FHairDescriptionBulkData::GetIdString() const
{
	FString GuidString = Guid.ToString();
	GuidString += TEXT("X");
	return GuidString;
}

void FHairDescriptionBulkData::ComputeGuidFromHash()
{
	uint32 Hash[5] = {};

	if (BulkData.GetBulkDataSize() > 0)
	{
		void* Buffer = BulkData.Lock(LOCK_READ_ONLY);
		FSHA1::HashBuffer(Buffer, BulkData.GetBulkDataSize(), (uint8*)Hash);
		BulkData.Unlock();
	}

	Guid = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
}

#endif // #if WITH_EDITORONLY_DATA
