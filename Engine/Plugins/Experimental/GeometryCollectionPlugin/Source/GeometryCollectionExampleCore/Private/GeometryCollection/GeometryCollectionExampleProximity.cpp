// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleProximity.h"

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
	bool BuildProximity(ExampleResponse&& R)
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.f, 0.f, 0.f)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.5f, 0.f, 1.f)),FVector(1.0)));

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

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);

		FGeometryCollectionProximityUtility::UpdateProximity(Coll);

		// Breaking Data
		TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue((Proximity)[2].Contains(1));

		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(0));

		return !R.HasError();
	}
	template bool BuildProximity<float>(ExampleResponse&& R);

	template<class T>
	bool GeometryDeleteFromStart(ExampleResponse&& R)
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.f, 0.f, 0.f)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(2.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(-0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.5f, 0.f, 1.f)),FVector(1.0)));

		//  0
		//  ...1
		//  ......2
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(3);

		(Collection->Parent)[3] = 0;
		(Collection->Children)[3].Add(4);

		(Collection->Parent)[4] = 0;
		(Collection->Children)[4].Add(5);

		(Collection->Parent)[5] = 0;
		//		GeometryCollectionAlgo::ParentTransform(Collection, 0, 1);

		FGeometryCollection* Coll = Collection.Get();

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);

		FGeometryCollectionProximityUtility::UpdateProximity(Coll);

		// Breaking Data
		TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

		// Proximity = [(3,4,1), (0,4,5,2), (1,5), (0,4), (0,1,3,5), (1,2,4)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(4));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue((Proximity)[1].Contains(5));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue((Proximity)[2].Contains(5));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(4));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(1));
		R.ExpectTrue(!(Proximity)[3].Contains(2));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(0));
		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue((Proximity)[4].Contains(5));
		R.ExpectTrue(!(Proximity)[4].Contains(2));
		R.ExpectTrue(!(Proximity)[4].Contains(4));

		R.ExpectTrue((Proximity)[5].Contains(1));
		R.ExpectTrue((Proximity)[5].Contains(2));
		R.ExpectTrue((Proximity)[5].Contains(4));
		R.ExpectTrue(!(Proximity)[5].Contains(0));
		R.ExpectTrue(!(Proximity)[5].Contains(3));
		R.ExpectTrue(!(Proximity)[5].Contains(5));

		TArray<int32> DelList = { 0 };
		Coll->RemoveElements(FGeometryCollection::GeometryGroup, DelList);

		// Proximity = [(3,4,1), (0,4), (3), (0,2,4), (0,1,3)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(4));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(3));
		R.ExpectTrue(!(Proximity)[1].Contains(5));

		R.ExpectTrue((Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(5));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(1));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(4));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(2));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(1));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(0));
		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue(!(Proximity)[4].Contains(2));
		R.ExpectTrue(!(Proximity)[4].Contains(4));
		R.ExpectTrue(!(Proximity)[4].Contains(5));

		R.ExpectTrue(Collection->NumElements(FGeometryCollection::GeometryGroup) == 5);

		return !R.HasError();
	}
	template bool GeometryDeleteFromStart<float>(ExampleResponse&& R);



	template<class T>
	bool GeometryDeleteFromEnd(ExampleResponse&& R)
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.f, 0.f, 0.f)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(2.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(-0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.5f, 0.f, 1.f)),FVector(1.0)));

		//  0
		//  ...1
		//  ......2
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(3);

		(Collection->Parent)[3] = 0;
		(Collection->Children)[3].Add(4);

		(Collection->Parent)[4] = 0;
		(Collection->Children)[4].Add(5);

		(Collection->Parent)[5] = 0;
		//		GeometryCollectionAlgo::ParentTransform(Collection, 0, 1);

		FGeometryCollection* Coll = Collection.Get();

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);

		FGeometryCollectionProximityUtility::UpdateProximity(Coll);

		// Breaking Data
		TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

		// Proximity = [(3,4,1), (0,4,5,2), (1,5), (0,4), (0,1,3,5), (1,2,4)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(4));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue((Proximity)[1].Contains(5));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue((Proximity)[2].Contains(5));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(4));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(1));
		R.ExpectTrue(!(Proximity)[3].Contains(2));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(0));
		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue((Proximity)[4].Contains(5));
		R.ExpectTrue(!(Proximity)[4].Contains(2));
		R.ExpectTrue(!(Proximity)[4].Contains(4));

		R.ExpectTrue((Proximity)[5].Contains(1));
		R.ExpectTrue((Proximity)[5].Contains(2));
		R.ExpectTrue((Proximity)[5].Contains(4));
		R.ExpectTrue(!(Proximity)[5].Contains(0));
		R.ExpectTrue(!(Proximity)[5].Contains(3));
		R.ExpectTrue(!(Proximity)[5].Contains(5));

		TArray<int32> DelList = { 5 };
		Coll->RemoveElements(FGeometryCollection::GeometryGroup, DelList);

		// Proximity = [(3,4,1), (0,4,2), (1), (0,4), (0,1,3)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(4));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));
		R.ExpectTrue(!(Proximity)[1].Contains(5));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue(!(Proximity)[2].Contains(5));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(4));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(2));
		R.ExpectTrue(!(Proximity)[3].Contains(1));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(0));
		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue(!(Proximity)[4].Contains(2));
		R.ExpectTrue(!(Proximity)[4].Contains(4));
		R.ExpectTrue(!(Proximity)[4].Contains(5));

		R.ExpectTrue(Collection->NumElements(FGeometryCollection::GeometryGroup) == 5);

		return !R.HasError();
	}
	template bool GeometryDeleteFromEnd<float>(ExampleResponse&& R);

	template<class T>
	bool GeometryDeleteFromMiddle(ExampleResponse&& R)
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.f, 0.f, 0.f)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(2.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(-0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.5f, 0.f, 1.f)),FVector(1.0)));

		//  0
		//  ...1
		//  ......2
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(3);

		(Collection->Parent)[3] = 0;
		(Collection->Children)[3].Add(4);

		(Collection->Parent)[4] = 0;
		(Collection->Children)[4].Add(5);

		(Collection->Parent)[5] = 0;
		//		GeometryCollectionAlgo::ParentTransform(Collection, 0, 1);

		FGeometryCollection* Coll = Collection.Get();

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);

		FGeometryCollectionProximityUtility::UpdateProximity(Coll);

		// Breaking Data
		TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

		// Proximity = [(3,4,1), (0,4,5,2), (1,5), (0,4), (0,1,3,5), (1,2,4)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(4));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue((Proximity)[1].Contains(5));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue((Proximity)[2].Contains(5));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(4));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(1));
		R.ExpectTrue(!(Proximity)[3].Contains(2));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(0));
		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue((Proximity)[4].Contains(5));
		R.ExpectTrue(!(Proximity)[4].Contains(2));
		R.ExpectTrue(!(Proximity)[4].Contains(4));

		R.ExpectTrue((Proximity)[5].Contains(1));
		R.ExpectTrue((Proximity)[5].Contains(2));
		R.ExpectTrue((Proximity)[5].Contains(4));
		R.ExpectTrue(!(Proximity)[5].Contains(0));
		R.ExpectTrue(!(Proximity)[5].Contains(3));
		R.ExpectTrue(!(Proximity)[5].Contains(5));

		TArray<int32> DelList = { 3 };
		Coll->RemoveElements(FGeometryCollection::GeometryGroup, DelList);

		// Proximity = [(3,1), (0,3,4,2), (1,4), (0,1,4), (1,2,3)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(4));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

	
		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(3));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(5));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue((Proximity)[2].Contains(4));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(5));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(1));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(2));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(2));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue(!(Proximity)[4].Contains(0));
		R.ExpectTrue(!(Proximity)[4].Contains(4));
		R.ExpectTrue(!(Proximity)[4].Contains(5));

		R.ExpectTrue(Collection->NumElements(FGeometryCollection::GeometryGroup) == 5);

		return !R.HasError();
	}
	template bool GeometryDeleteFromMiddle<float>(ExampleResponse&& R);

	template<class T>
	bool GeometryDeleteMultipleFromMiddle(ExampleResponse&& R)
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.f, 0.f, 0.f)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(2.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(-0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.5f, 0.f, 1.f)),FVector(1.0)));

		//  0
		//  ...1
		//  ......2
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(3);

		(Collection->Parent)[3] = 0;
		(Collection->Children)[3].Add(4);

		(Collection->Parent)[4] = 0;
		(Collection->Children)[4].Add(5);

		(Collection->Parent)[5] = 0;
		//		GeometryCollectionAlgo::ParentTransform(Collection, 0, 1);

		FGeometryCollection* Coll = Collection.Get();

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);

		FGeometryCollectionProximityUtility::UpdateProximity(Coll);

		// Breaking Data
		TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

		// Proximity = [(3,4,1), (0,4,5,2), (1,5), (0,4), (0,1,3,5), (1,2,4)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(4));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue((Proximity)[1].Contains(5));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue((Proximity)[2].Contains(5));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(4));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(1));
		R.ExpectTrue(!(Proximity)[3].Contains(2));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(0));
		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue((Proximity)[4].Contains(5));
		R.ExpectTrue(!(Proximity)[4].Contains(2));
		R.ExpectTrue(!(Proximity)[4].Contains(4));

		R.ExpectTrue((Proximity)[5].Contains(1));
		R.ExpectTrue((Proximity)[5].Contains(2));
		R.ExpectTrue((Proximity)[5].Contains(4));
		R.ExpectTrue(!(Proximity)[5].Contains(0));
		R.ExpectTrue(!(Proximity)[5].Contains(3));
		R.ExpectTrue(!(Proximity)[5].Contains(5));

		TArray<int32> DelList = { 2,3,4 };
		Coll->RemoveElements(FGeometryCollection::GeometryGroup, DelList);

		// Proximity = [(1), (0,2), (1)]

		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(3));
		R.ExpectTrue(!(Proximity)[0].Contains(4));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));
		R.ExpectTrue(!(Proximity)[1].Contains(4));
		R.ExpectTrue(!(Proximity)[1].Contains(5));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue(!(Proximity)[2].Contains(4));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(5));

		R.ExpectTrue(Collection->NumElements(FGeometryCollection::GeometryGroup) == 3);

		return !R.HasError();
	}
	template bool GeometryDeleteMultipleFromMiddle<float>(ExampleResponse&& R);

	template<class T>
	bool GeometryDeleteRandom(ExampleResponse&& R)
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.f, 0.f, 0.f)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(2.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(-0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.5f, 0.f, 1.f)),FVector(1.0)));

		//  0
		//  ...1
		//  ......2
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(3);

		(Collection->Parent)[3] = 0;
		(Collection->Children)[3].Add(4);

		(Collection->Parent)[4] = 0;
		(Collection->Children)[4].Add(5);

		(Collection->Parent)[5] = 0;
		//		GeometryCollectionAlgo::ParentTransform(Collection, 0, 1);

		FGeometryCollection* Coll = Collection.Get();

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);

		FGeometryCollectionProximityUtility::UpdateProximity(Coll);

		// Breaking Data
		TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

		// Proximity = [(3,4,1), (0,4,5,2), (1,5), (0,4), (0,1,3,5), (1,2,4)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(4));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue((Proximity)[1].Contains(5));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue((Proximity)[2].Contains(5));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(4));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(1));
		R.ExpectTrue(!(Proximity)[3].Contains(2));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(0));
		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue((Proximity)[4].Contains(5));
		R.ExpectTrue(!(Proximity)[4].Contains(2));
		R.ExpectTrue(!(Proximity)[4].Contains(4));

		R.ExpectTrue((Proximity)[5].Contains(1));
		R.ExpectTrue((Proximity)[5].Contains(2));
		R.ExpectTrue((Proximity)[5].Contains(4));
		R.ExpectTrue(!(Proximity)[5].Contains(0));
		R.ExpectTrue(!(Proximity)[5].Contains(3));
		R.ExpectTrue(!(Proximity)[5].Contains(5));

		TArray<int32> DelList = { 1,3,5 };
		Coll->RemoveElements(FGeometryCollection::GeometryGroup, DelList);

		// Proximity = [(2), (), (0)]

		R.ExpectTrue((Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(3));
		R.ExpectTrue(!(Proximity)[0].Contains(4));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue(!(Proximity)[1].Contains(0));
		R.ExpectTrue(!(Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));
		R.ExpectTrue(!(Proximity)[1].Contains(4));
		R.ExpectTrue(!(Proximity)[1].Contains(5));

		R.ExpectTrue((Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(1));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(4));
		R.ExpectTrue(!(Proximity)[2].Contains(5));

		R.ExpectTrue(Collection->NumElements(FGeometryCollection::GeometryGroup) == 3);

		return !R.HasError();
	}
	template bool GeometryDeleteRandom<float>(ExampleResponse&& R); 

	template<class T>
	bool GeometryDeleteRandom2(ExampleResponse&& R)
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.f, 0.f, 0.f)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(2.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(-0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.5f, 0.f, 1.f)),FVector(1.0)));

		//  0
		//  ...1
		//  ......2
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(3);

		(Collection->Parent)[3] = 0;
		(Collection->Children)[3].Add(4);

		(Collection->Parent)[4] = 0;
		(Collection->Children)[4].Add(5);

		(Collection->Parent)[5] = 0;
		//		GeometryCollectionAlgo::ParentTransform(Collection, 0, 1);

		FGeometryCollection* Coll = Collection.Get();

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);

		FGeometryCollectionProximityUtility::UpdateProximity(Coll);

		// Breaking Data
		TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

		// Proximity = [(3,4,1), (0,4,5,2), (1,5), (0,4), (0,1,3,5), (1,2,4)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(4));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue((Proximity)[1].Contains(5));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue((Proximity)[2].Contains(5));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(4));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(1));
		R.ExpectTrue(!(Proximity)[3].Contains(2));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(0));
		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue((Proximity)[4].Contains(5));
		R.ExpectTrue(!(Proximity)[4].Contains(2));
		R.ExpectTrue(!(Proximity)[4].Contains(4));

		R.ExpectTrue((Proximity)[5].Contains(1));
		R.ExpectTrue((Proximity)[5].Contains(2));
		R.ExpectTrue((Proximity)[5].Contains(4));
		R.ExpectTrue(!(Proximity)[5].Contains(0));
		R.ExpectTrue(!(Proximity)[5].Contains(3));
		R.ExpectTrue(!(Proximity)[5].Contains(5));

		TArray<int32> DelList = { 0,1,4,5 };
		Coll->RemoveElements(FGeometryCollection::GeometryGroup, DelList);

		// Proximity = [(), ()]

		R.ExpectTrue(!(Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(3));
		R.ExpectTrue(!(Proximity)[0].Contains(4));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue(!(Proximity)[1].Contains(0));
		R.ExpectTrue(!(Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));
		R.ExpectTrue(!(Proximity)[1].Contains(4));
		R.ExpectTrue(!(Proximity)[1].Contains(5));

		R.ExpectTrue(Collection->NumElements(FGeometryCollection::GeometryGroup) == 2);

		return !R.HasError();
	}
	template bool GeometryDeleteRandom2<float>(ExampleResponse&& R);

	template<class T>
	bool GeometryDeleteAll(ExampleResponse&& R)
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.f, 0.f, 0.f)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(2.f, 0.f, 0.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(-0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(0.5f, 0.f, 1.f)),FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f, 0.f, 0.f)), FVector(1.5f, 0.f, 1.f)),FVector(1.0)));

		//  0
		//  ...1
		//  ......2
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(3);

		(Collection->Parent)[3] = 0;
		(Collection->Children)[3].Add(4);

		(Collection->Parent)[4] = 0;
		(Collection->Children)[4].Add(5);

		(Collection->Parent)[5] = 0;
		//		GeometryCollectionAlgo::ParentTransform(Collection, 0, 1);

		FGeometryCollection* Coll = Collection.Get();

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);

		FGeometryCollectionProximityUtility::UpdateProximity(Coll);

		// Breaking Data
		TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

		// Proximity = [(3,4,1), (0,4,5,2), (1,5), (0,4), (0,1,3,5), (1,2,4)]

		R.ExpectTrue((Proximity)[0].Contains(3));
		R.ExpectTrue((Proximity)[0].Contains(4));
		R.ExpectTrue((Proximity)[0].Contains(1));
		R.ExpectTrue(!(Proximity)[0].Contains(0));
		R.ExpectTrue(!(Proximity)[0].Contains(2));
		R.ExpectTrue(!(Proximity)[0].Contains(5));

		R.ExpectTrue((Proximity)[1].Contains(0));
		R.ExpectTrue((Proximity)[1].Contains(4));
		R.ExpectTrue((Proximity)[1].Contains(5));
		R.ExpectTrue((Proximity)[1].Contains(2));
		R.ExpectTrue(!(Proximity)[1].Contains(1));
		R.ExpectTrue(!(Proximity)[1].Contains(3));

		R.ExpectTrue((Proximity)[2].Contains(1));
		R.ExpectTrue((Proximity)[2].Contains(5));
		R.ExpectTrue(!(Proximity)[2].Contains(0));
		R.ExpectTrue(!(Proximity)[2].Contains(2));
		R.ExpectTrue(!(Proximity)[2].Contains(3));
		R.ExpectTrue(!(Proximity)[2].Contains(4));

		R.ExpectTrue((Proximity)[3].Contains(0));
		R.ExpectTrue((Proximity)[3].Contains(4));
		R.ExpectTrue(!(Proximity)[3].Contains(1));
		R.ExpectTrue(!(Proximity)[3].Contains(2));
		R.ExpectTrue(!(Proximity)[3].Contains(3));
		R.ExpectTrue(!(Proximity)[3].Contains(5));

		R.ExpectTrue((Proximity)[4].Contains(0));
		R.ExpectTrue((Proximity)[4].Contains(1));
		R.ExpectTrue((Proximity)[4].Contains(3));
		R.ExpectTrue((Proximity)[4].Contains(5));
		R.ExpectTrue(!(Proximity)[4].Contains(2));
		R.ExpectTrue(!(Proximity)[4].Contains(4));

		R.ExpectTrue((Proximity)[5].Contains(1));
		R.ExpectTrue((Proximity)[5].Contains(2));
		R.ExpectTrue((Proximity)[5].Contains(4));
		R.ExpectTrue(!(Proximity)[5].Contains(0));
		R.ExpectTrue(!(Proximity)[5].Contains(3));
		R.ExpectTrue(!(Proximity)[5].Contains(5));

		TArray<int32> DelList = { 0,1,2,3,4,5 };
		Coll->RemoveElements(FGeometryCollection::GeometryGroup, DelList);

		// Proximity = []

		R.ExpectTrue(Collection->NumElements(FGeometryCollection::GeometryGroup) == 0);

		return !R.HasError();
	}
	template bool GeometryDeleteAll<float>(ExampleResponse&& R);

	template<class T>
	bool GeometrySwapFlat(ExampleResponse&& R)
	{
		FGeometryCollection Coll;
		Coll.AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0,0,0))));
		Coll.AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(1,0,0))));
		Coll.AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(2,0,0))));
		Coll.AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(3,0,0))));
		
		//verts are ordered with geometry
		for (int box = 0; box < 4; ++box)
		{
			for (int i = 0; i < 8; ++i)
			{
				R.ExpectTrue(Coll.BoneMap[i + box * 8] == box);
			}
		}

		// Bottom: Y = -1
		int32 ExpectedIndices[] = {
			5,1,0,
			0,4,5,
			2,3,7,
			7,6,2,
			3,2,0,
			0,1,3,
			4,6,7,
			7,5,4,
			0,2,6,
			6,4,0,
			7,3,1,
			1,5,7 };

		//faces are ordered with geometry and point to the right vertices
		for (int box = 0; box < 4; ++box)
		{
			for (int i = 0; i < 12; ++i)
			{
				for (int idx = 0; idx < 3; ++idx)
				{
					R.ExpectTrue(Coll.Indices[i + box * 12][idx] == (ExpectedIndices[i * 3 + idx] + box * 8));
				}
			}
		}

		TArray<int32> NewOrder = { 0,3,2,1 };
		Coll.ReorderElements(FGeometryCollection::TransformGroup, NewOrder);
		//transforms change
		R.ExpectTrue(Coll.Transform[0].GetLocation().X == 0.f);
		R.ExpectTrue(Coll.Transform[1].GetLocation().X == 3.f);
		R.ExpectTrue(Coll.Transform[2].GetLocation().X == 2.f);
		R.ExpectTrue(Coll.Transform[3].GetLocation().X == 1.f);

		//groups swap to be contiguous with transform array
		R.ExpectTrue(Coll.TransformIndex[0] == 0);
		R.ExpectTrue(Coll.TransformIndex[1] == 1);
		R.ExpectTrue(Coll.TransformIndex[2] == 2);
		R.ExpectTrue(Coll.TransformIndex[3] == 3);

		//verts are still contiguous
		for (int box = 0; box < 4; ++box)
		{
			for (int i = 0; i < 8; ++i)
			{
				R.ExpectTrue(Coll.BoneMap[i + box * 8] == box);
			}
		}

		//faces are reordered with geometry and point to the right vertices
		for (int box = 0; box < 4; ++box)
		{
			for (int i = 0; i < 12; ++i)
			{
				for (int idx = 0; idx < 3; ++idx)
				{
					//expect verts to reorder with faces so there's no indirection needed. The whole point is that we are contiguous
					R.ExpectTrue(Coll.Indices[i + box * 12][idx] == (ExpectedIndices[i * 3 + idx] + box * 8));
				}
			}
		}

		return !R.HasError();
	}
	template bool GeometrySwapFlat<float>(ExampleResponse&& R);

	
	template<class T>
	bool TestFracturedGeometry(ExampleResponse&& R)
	{
		FGeometryCollection* TestCollection = FGeometryCollection::NewGeometryCollection(FracturedGeometry::RawVertexArray,
																						 FracturedGeometry::RawIndicesArray,
																						 FracturedGeometry::RawBoneMapArray,
																						 FracturedGeometry::RawTransformArray,
																						 FracturedGeometry::RawLevelArray,
																						 FracturedGeometry::RawParentArray,
																						 FracturedGeometry::RawChildrenArray,
																						 FracturedGeometry::RawSimulationTypeArray,
																						 FracturedGeometry::RawStatusFlagsArray);

		R.ExpectTrue(TestCollection->NumElements(FGeometryCollection::GeometryGroup) == 11);

		return !R.HasError();
	}
	template bool TestFracturedGeometry<float>(ExampleResponse&& R);


}

