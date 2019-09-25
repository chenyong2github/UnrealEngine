// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairDescription.h"

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