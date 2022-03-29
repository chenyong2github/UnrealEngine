// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FleshCollection.cpp: FFleshCollection methods.
=============================================================================*/

#include "ChaosFlesh/FleshCollection.h"

#include "ChaosFlesh/FleshCollectionUtility.h"

DEFINE_LOG_CATEGORY_STATIC(FFleshCollectionLogging, Log, All);


// Attributes
const FName FFleshCollection::MassAttribute("Mass");

FFleshCollection::FFleshCollection()
	: FTetrahedralCollection()
{
	Construct();
}


void FFleshCollection::Construct()
{
	// Vertices Group
	AddExternalAttribute<float>(FFleshCollection::MassAttribute, FFleshCollection::VerticesGroup, Mass);
	for (float& m : Mass) { m = 1; }
}


FFleshCollection* FFleshCollection::NewFleshCollection(const TArray<FVector>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements, bool bReverseVertexOrder)
{

	FFleshCollection* Collection = new FFleshCollection();
	FFleshCollection::Init(Collection, Vertices,SurfaceElements, Elements, bReverseVertexOrder);
	return Collection;
}
FFleshCollection* FFleshCollection::NewFleshCollection(const FTetrahedralCollection& Base)
{
	FFleshCollection* Collection = new FFleshCollection();
	Collection->CopyMatchingAttributesFrom(Base);
	for (float& m : Collection->Mass) { m = 1; }
	return Collection;
}

void FFleshCollection::Init(FFleshCollection* Collection, const TArray<FVector>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements, bool bReverseVertexOrder)
{
	if (Collection)
	{
		FTetrahedralCollection::Init(Collection, Vertices, SurfaceElements, Elements, bReverseVertexOrder);
	}
}


