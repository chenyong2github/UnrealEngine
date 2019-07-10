// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleClean.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/TransformCollection.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "../Resource/FracturedGeometry.h"

namespace GeometryCollectionExample
{
	template<class T>
	bool TestDeleteCoincidentVertices(ExampleResponse&& R)
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.f, 0.f, 0.f)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.f, 0.f, 0.f)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.5f, 0.f, 1.f)), FVector(1.0)));

		//  0
		//  ...1
		//  ......2
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);
		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);
		(Collection->Parent)[2] = 1;
//		GeometryCollectionAlgo::ParentTransform(Collection, 0, 1);

		FGeometryCollection* Coll = Collection.Get();

		GeometryCollectionAlgo::DeleteCoincidentVertices(Coll, 1e-2);

		
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 24);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 36);

		GeometryCollectionAlgo::DeleteZeroAreaFaces(Coll, 1e-4);

		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 24);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 36);

		return !R.HasError();
	}
	template bool TestDeleteCoincidentVertices<float>(ExampleResponse&& R);

	

	template<class T>
	bool TestDeleteCoincidentVertices2(ExampleResponse&& R)
	{
		FGeometryCollection* Coll = FGeometryCollection::NewGeometryCollection(FracturedGeometry::RawVertexArray,
																   			   FracturedGeometry::RawIndicesArray,
																			   FracturedGeometry::RawBoneMapArray,
																			   FracturedGeometry::RawTransformArray,
																			   FracturedGeometry::RawLevelArray,
																			   FracturedGeometry::RawParentArray,
																			   FracturedGeometry::RawChildrenArray,
																			   FracturedGeometry::RawSimulationTypeArray,
																			   FracturedGeometry::RawStatusFlagsArray);

		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 667);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 493);

		GeometryCollectionAlgo::DeleteCoincidentVertices(Coll, 1e-2);

		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 270);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 493);

		return !R.HasError();
	}
	template bool TestDeleteCoincidentVertices2<float>(ExampleResponse&& R);

	template<class T>
	bool TestDeleteZeroAreaFaces(ExampleResponse&& R)
	{
		FGeometryCollection* Coll = FGeometryCollection::NewGeometryCollection(FracturedGeometry::RawVertexArray,
																			   FracturedGeometry::RawIndicesArray,
																			   FracturedGeometry::RawBoneMapArray,
																			   FracturedGeometry::RawTransformArray,
																			   FracturedGeometry::RawLevelArray,
																			   FracturedGeometry::RawParentArray,
																			   FracturedGeometry::RawChildrenArray,
																			   FracturedGeometry::RawSimulationTypeArray,
																			   FracturedGeometry::RawStatusFlagsArray
			);

		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 667);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 493);

		GeometryCollectionAlgo::DeleteZeroAreaFaces(Coll, 1e-4);

		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 667);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 493);

		return !R.HasError();
	}
	template bool TestDeleteZeroAreaFaces<float>(ExampleResponse&& R);

	template<class T>
	bool TestFillHoles(ExampleResponse&& R)
	{
		FGeometryCollection* Coll = FGeometryCollection::NewGeometryCollection(FracturedGeometry::RawVertexArray,
			FracturedGeometry::RawIndicesArray,
			FracturedGeometry::RawBoneMapArray,
			FracturedGeometry::RawTransformArray,
			FracturedGeometry::RawLevelArray,
			FracturedGeometry::RawParentArray,
			FracturedGeometry::RawChildrenArray,
			FracturedGeometry::RawSimulationTypeArray,
			FracturedGeometry::RawStatusFlagsArray);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 667);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 493);

		TArray<TArray<TArray<int32>>> BoundaryVertexIndices;
		Coll->RemoveElements(FGeometryCollection::FacesGroup, { 0,1,2 });

		GeometryCollectionAlgo::FindOpenBoundaries(Coll, 1e-2, BoundaryVertexIndices);

		auto CountHoles = [](const TArray<TArray<TArray<int32>>> &InBoundaryVertexIndices)
		{
			int NumHoles = 0;
			for (const TArray<TArray<int32>> &GeomBoundaries : InBoundaryVertexIndices)
			{
				NumHoles += GeomBoundaries.Num();
			}
			return NumHoles;
		};

		auto CountTinyFaces = [](const FGeometryCollection *InColl, float InTinyNumber = 1e-4)
		{
			int TinyFaces = 0;
			for (const FIntVector & Face : InColl->Indices)
			{
				FVector p10 = InColl->Vertex[Face.Y] - InColl->Vertex[Face.X];
				FVector p20 = InColl->Vertex[Face.Z] - InColl->Vertex[Face.X];
				FVector Cross = FVector::CrossProduct(p20, p10);
				if (Cross.SizeSquared() < InTinyNumber)
				{
					TinyFaces++;
				}
			}
			return TinyFaces;
		};
		
		int32 TinyFacesBefore = CountTinyFaces(Coll);
		R.ExpectTrue(CountHoles(BoundaryVertexIndices) == 3);
		GeometryCollectionAlgo::TriangulateBoundaries(Coll, BoundaryVertexIndices);
		int32 TinyFacesAfter = CountTinyFaces(Coll);
		R.ExpectTrue(CountTinyFaces(Coll) == TinyFacesBefore);

		BoundaryVertexIndices.Empty();
		GeometryCollectionAlgo::FindOpenBoundaries(Coll, 1e-2, BoundaryVertexIndices);
		R.ExpectTrue(CountHoles(BoundaryVertexIndices) == 2);

		GeometryCollectionAlgo::TriangulateBoundaries(Coll, BoundaryVertexIndices, true, 0);
		BoundaryVertexIndices.Empty();
		GeometryCollectionAlgo::FindOpenBoundaries(Coll, 1e-2, BoundaryVertexIndices);
		R.ExpectTrue(CountHoles(BoundaryVertexIndices) == 0);
		R.ExpectTrue(CountTinyFaces(Coll) > TinyFacesBefore);


		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 667);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 496);

		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 667);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 496);

		R.ExpectTrue(Coll->HasContiguousFaces());
		R.ExpectTrue(Coll->HasContiguousVertices());
		R.ExpectTrue(GeometryCollectionAlgo::HasValidGeometryReferences(Coll));

		return !R.HasError();
	}
	template bool TestFillHoles<float>(ExampleResponse&& R);

	template<class T>
	bool TestDeleteHiddenFaces(ExampleResponse&& R)
	{
		FGeometryCollection* Coll = FGeometryCollection::NewGeometryCollection(FracturedGeometry::RawVertexArray,
																			   FracturedGeometry::RawIndicesArray,
																			   FracturedGeometry::RawBoneMapArray,
																			   FracturedGeometry::RawTransformArray,
																			   FracturedGeometry::RawLevelArray,
																			   FracturedGeometry::RawParentArray,
																			   FracturedGeometry::RawChildrenArray,
																			   FracturedGeometry::RawSimulationTypeArray,
																			   FracturedGeometry::RawStatusFlagsArray);

		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 667);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 493);

		TManagedArray<bool>& VisibleArray = Coll->Visible;

		int32 NumFaces = Coll->NumElements(FGeometryCollection::FacesGroup);
		for (int32 Idx = 0; Idx < NumFaces; ++Idx)
		{
			if (!(Idx % 5))
			{
				VisibleArray[Idx] = false;
			}
		}

		R.ExpectTrue(Coll->NumElements(FGeometryCollection::VerticesGroup) == 667);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 493);

		GeometryCollectionAlgo::DeleteHiddenFaces(Coll);

		R.ExpectTrue( Coll->NumElements(FGeometryCollection::VerticesGroup) == 667);
		R.ExpectTrue(Coll->NumElements(FGeometryCollection::FacesGroup) == 394);

		return !R.HasError();
	}
	template bool TestDeleteHiddenFaces<float>(ExampleResponse&& R);
}

