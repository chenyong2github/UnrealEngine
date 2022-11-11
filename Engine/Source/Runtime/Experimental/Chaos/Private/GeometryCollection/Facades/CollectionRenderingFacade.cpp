// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "GeometryCollection/GeometryCollection.h"

namespace GeometryCollection::Facades
{

	FRenderingFacade::FRenderingFacade(FManagedArrayCollection* InCollection)
		: Self(InCollection)
	{
		DefineSchema(Self);
	}

	//
	//  Initialization
	//

	void FRenderingFacade::DefineSchema(FManagedArrayCollection* InCollection)
	{
		FManagedArrayCollection::FConstructionParameters VertexDependency(FGeometryCollection::VerticesGroup);

		// surface rendering attributes
		if (!InCollection->HasGroup(FGeometryCollection::VerticesGroup))
		{
			InCollection->AddGroup(FGeometryCollection::VerticesGroup);
		}
		if (!InCollection->HasGroup(FGeometryCollection::FacesGroup))
		{
			InCollection->AddGroup(FGeometryCollection::FacesGroup);
		}

		InCollection->AddAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
		InCollection->AddAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup, VertexDependency);

		ensure(InCollection->FindAttributeTyped<FVector3f>("Vertex", FGeometryCollection::VerticesGroup) != nullptr);
		ensure(InCollection->FindAttributeTyped<FIntVector>("Indices", FGeometryCollection::FacesGroup) != nullptr);
	}

	bool FRenderingFacade::CanRenderSurface(const FManagedArrayCollection* InCollection)
	{
		return InCollection->HasGroup(FGeometryCollection::VerticesGroup)
			&& InCollection->HasGroup(FGeometryCollection::FacesGroup)
			&& InCollection->FindAttributeTyped<FVector3f>("Vertex", FGeometryCollection::VerticesGroup)
			&& InCollection->FindAttributeTyped<FIntVector>("Indices", FGeometryCollection::FacesGroup);
	}

	bool FRenderingFacade::IsValid(const FManagedArrayCollection* InCollection)
	{
		return FRenderingFacade::CanRenderSurface(InCollection);
	}

	void FRenderingFacade::AddTriangle(FManagedArrayCollection* InCollection, const Chaos::FTriangle& InTriangle)
	{
		if (IsValid(InCollection))
		{
			auto CollectionVert = [](const Chaos::FVec3& V) { return FVector3f(float(V.X), float(V.Y), float(V.Z)); };

			TManagedArray<FVector3f>& Vertices = InCollection->ModifyAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
			TManagedArray<FIntVector>& Indices = InCollection->ModifyAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);

			int32 IndicesStart = InCollection->AddElements(1, FGeometryCollection::FacesGroup);
			int32 VertexStart = InCollection->AddElements(3, FGeometryCollection::VerticesGroup);

			Indices[IndicesStart] = FIntVector(VertexStart, VertexStart + 1, VertexStart + 2);
			Vertices[VertexStart] = CollectionVert(InTriangle[0]);
			Vertices[VertexStart + 1] = CollectionVert(InTriangle[1]);
			Vertices[VertexStart + 2] = CollectionVert(InTriangle[2]);
		}
	}


	void FRenderingFacade::AddSurface(FManagedArrayCollection* InCollection, TArray<FVector3f>&& InVertices, TArray<FIntVector>&& InIndices)
	{
		if (IsValid(InCollection))
		{
			auto CollectionVert = [](const Chaos::FVec3& V) { return FVector3f(float(V.X), float(V.Y), float(V.Z)); };

			TManagedArray<FVector3f>& Vertices = InCollection->ModifyAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
			TManagedArray<FIntVector>& Indices = InCollection->ModifyAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);

			int32 IndicesStart = InCollection->AddElements(InIndices.Num(), FGeometryCollection::FacesGroup);
			int32 VertexStart = InCollection->AddElements(InVertices.Num(), FGeometryCollection::VerticesGroup);

			const FIntVector * IndiciesDest = Indices.GetData() + IndicesStart;
			FMemory::Memmove((void*)IndiciesDest, InIndices.GetData(), sizeof(FIntVector) * InIndices.Num());

			const FVector3f * VerticesDest = Vertices.GetData() + VertexStart;
			FMemory::Memmove((void*)VerticesDest, InVertices.GetData(), sizeof(FVector3f) * InVertices.Num());
		}
	}

	//
	//  GetAttributes
	//

	const TManagedArray< FIntVector >* FRenderingFacade::GetIndices(const FManagedArrayCollection* InCollection)
	{
		return InCollection->FindAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);
	}

	const TManagedArray< FVector3f >* FRenderingFacade::GetVertices(const FManagedArrayCollection* InCollection)
	{
		return InCollection->FindAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
	}


}; // GeometryCollection::Facades


