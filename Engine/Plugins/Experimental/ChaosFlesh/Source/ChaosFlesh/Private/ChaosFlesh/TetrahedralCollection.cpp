// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosFlesh/TetrahedralCollection.h"

#include "Chaos/ChaosArchive.h"

DEFINE_LOG_CATEGORY_STATIC(FTetrahedralCollectionLogging, Log, All);

// Groups
const FName FTetrahedralCollection::TetrahedralGroup = "Tetrahedral";

// Attributes
const FName FTetrahedralCollection::TetrahedronAttribute("Tetrahedron");

FTetrahedralCollection::FTetrahedralCollection()
	: Super::FGeometryCollection()
{
	Construct();
}


void FTetrahedralCollection::Construct()
{
	FManagedArrayCollection::FConstructionParameters TetrahedronDependency(FTetrahedralCollection::TetrahedralGroup);

	// Tetrahedron Group
	AddExternalAttribute<FIntVector4>(FTetrahedralCollection::TetrahedralGroup, FTetrahedralCollection::TetrahedralGroup, Tetrahedron);
}


void FTetrahedralCollection::SetDefaults(FName Group, uint32 StartSize, uint32 NumElements)
{
	Super::SetDefaults(Group, StartSize, NumElements);

	if (Group == FTetrahedralCollection::TetrahedralGroup)
	{
		for (uint32 Idx = StartSize; Idx < StartSize + NumElements; ++Idx)
		{
			Tetrahedron[Idx] = FIntVector4(INDEX_NONE);
		}
	}
}

FTetrahedralCollection* FTetrahedralCollection::NewTetrahedralCollection(const TArray<FVector3f>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements, bool bReverseVertexOrder)
{
	FTetrahedralCollection* Collection = new FTetrahedralCollection();
	FTetrahedralCollection::Init(Collection, Vertices, SurfaceElements, Elements, bReverseVertexOrder);
	return Collection;
}
void FTetrahedralCollection::Init(FTetrahedralCollection* Collection, const TArray<FVector3f>& Vertices, const TArray<FIntVector3>& SurfaceElements, const TArray<FIntVector4>& Elements, bool bReverseVertexOrder)
{
	if (Collection)
	{
		TArray<float> RawVertexArray;
		RawVertexArray.SetNum(Vertices.Num()*3);
		for(int i=0,j=0;i<Vertices.Num();i++,j+=3)
		{
			RawVertexArray[j+0] = Vertices[i].X;
			RawVertexArray[j+1] = Vertices[i].Y;
			RawVertexArray[j+2] = Vertices[i].Z;
		}
		TArray<int32> RawIndicesArray;
		RawIndicesArray.SetNum(SurfaceElements.Num()*3);
		for (int i = 0, j = 0; i < Vertices.Num(); i++, j += 3)
		{
			RawIndicesArray[j + 0] = SurfaceElements[i].X;
			RawIndicesArray[j + 1] = SurfaceElements[i].Y;
			RawIndicesArray[j + 2] = SurfaceElements[i].Z;
		}

		Super::Init(Collection, RawVertexArray, RawIndicesArray, bReverseVertexOrder);

		Collection->AddElements(Elements.Num(), FTetrahedralCollection::TetrahedralGroup);
		for (int i = 0; i < Elements.Num(); i++)
		{
			Collection->Tetrahedron[i] = Elements[i];
		}
	}
}


