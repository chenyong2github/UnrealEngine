// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleCreation.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/TransformCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(GCTCR_Log, Verbose, All);


namespace GeometryCollectionExample
{

	template<class T>
	void CheckIncrementMask()
	{
		{
			TArray<int32> Mask;
			GeometryCollectionAlgo::BuildIncrementMask({ 2 }, 5, Mask);
			EXPECT_EQ(Mask[2], 0);
			EXPECT_EQ(Mask[3], 1);
		}
		{
			TArray<int32> Mask;
			GeometryCollectionAlgo::BuildIncrementMask({ 0 }, 5, Mask);
			EXPECT_EQ(Mask[0], 0);
			EXPECT_EQ(Mask[1], 1);
		}
		{
			TArray<int32> Mask;
			GeometryCollectionAlgo::BuildIncrementMask({ 1,2 }, 5, Mask);
			EXPECT_EQ(Mask[0], 0);
			EXPECT_EQ(Mask[1], 0);
			EXPECT_EQ(Mask[2], 1);
			EXPECT_EQ(Mask[3], 2);
			EXPECT_EQ(Mask[4], 2);
		}
	}
	template void CheckIncrementMask<float>();

	template<class T>
	void Creation()
	{
		TSharedPtr<FGeometryCollection> Collection(new FGeometryCollection());

		GeometryCollection::SetupCubeGridExample(Collection);

		EXPECT_TRUE(Collection->HasGroup(FTransformCollection::TransformGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::VerticesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::FacesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::MaterialGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::GeometryGroup));

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 1000);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 8000);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 12000);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 1000);

		int HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));

		EXPECT_TRUE(Collection->HasContiguousFaces());
		EXPECT_TRUE(Collection->HasContiguousVertices());
		EXPECT_TRUE(Collection->HasContiguousRenderFaces());
	}
	template void Creation<float>();

	template<class T>
	void AppendTransformHierarchy()
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 90.)), FVector(0, 10, 0)), FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 90.)), FVector(0, 10, 0)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 90.)), FVector(0, 10, 0)), FVector(1.0)));

		TSharedPtr<FGeometryCollection> Collection2 = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 90.)), FVector(0, 10, 0)), FVector(1.0), 4);
		Collection2->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 90.)), FVector(0, 10, 0)), FVector(1.0), 4));
		Collection2->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 90.)), FVector(0, 10, 0)), FVector(1.0), 4));


		//  0
		//  ...1
		//  ......2
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);
		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);
		(Collection->Parent)[2] = 1;

		//  0
		//  ...1
		//  ...2
		(Collection2->Parent)[0] = -1;
		(Collection2->Children)[0].Add(1);
		(Collection2->Parent)[1] = 0;
		(Collection2->Children)[0].Add(2);
		(Collection2->Parent)[2] = 0;

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 3);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 24);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 36);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 3);

		EXPECT_EQ(Collection2->NumElements(FTransformCollection::TransformGroup), 3);
		EXPECT_EQ(Collection2->NumElements(FGeometryCollection::VerticesGroup), 24);
		EXPECT_EQ(Collection2->NumElements(FGeometryCollection::FacesGroup), 36);
		EXPECT_EQ(Collection2->NumElements(FGeometryCollection::MaterialGroup), 4);
		EXPECT_EQ(Collection2->NumElements(FGeometryCollection::GeometryGroup), 3);

		Collection->AppendGeometry(*Collection2);

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 6);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 48);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 72);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 4); // union of the 2/4 materials
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 6);

		EXPECT_EQ(Collection->Parent[0], -1);
		EXPECT_EQ(Collection->Parent[1], 0);
		EXPECT_EQ(Collection->Parent[2], 1);
		EXPECT_EQ(Collection->Parent[3], -1);
		EXPECT_EQ(Collection->Parent[4], 3);
		EXPECT_EQ(Collection->Parent[5], 3);

		EXPECT_EQ(Collection->Children[0].Num(), 1);
		EXPECT_EQ(Collection->Children[1].Num(), 1);
		EXPECT_EQ(Collection->Children[2].Num(), 0);
		EXPECT_EQ(Collection->Children[3].Num(), 2);
		EXPECT_EQ(Collection->Children[4].Num(), 0);
		EXPECT_EQ(Collection->Children[5].Num(), 0);

		EXPECT_EQ(Collection->Children[0].Array()[0], 1);
		EXPECT_EQ(Collection->Children[1].Array()[0], 2);
		EXPECT_EQ(Collection->Children[3].Array()[0], 4);
		EXPECT_EQ(Collection->Children[3].Array()[1], 5);

		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, 18 + 9);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, (Collection->Sections)[0].FirstIndex + (Collection->Sections)[0].NumTriangles * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, 18 + 9);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[2].MaterialID, 2);
		EXPECT_EQ((Collection->Sections)[2].FirstIndex, (Collection->Sections)[1].FirstIndex + (Collection->Sections)[1].NumTriangles * 3);
		EXPECT_EQ((Collection->Sections)[2].NumTriangles, 9);
		EXPECT_EQ((Collection->Sections)[2].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[2].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[3].MaterialID, 3);
		EXPECT_EQ((Collection->Sections)[3].FirstIndex, (Collection->Sections)[2].FirstIndex + (Collection->Sections)[2].NumTriangles * 3);
		EXPECT_EQ((Collection->Sections)[3].NumTriangles, 9);
		EXPECT_EQ((Collection->Sections)[3].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[3].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		// GeometryGroup Tests
		{
			EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 6);

			EXPECT_EQ((Collection->TransformIndex)[0], 0);
			EXPECT_EQ((Collection->TransformIndex)[1], 1);
			EXPECT_EQ((Collection->TransformIndex)[2], 2);
			EXPECT_EQ((Collection->TransformIndex)[3], 3);
			EXPECT_EQ((Collection->TransformIndex)[4], 4);
			EXPECT_EQ((Collection->TransformIndex)[5], 5);

			EXPECT_EQ((Collection->TransformToGeometryIndex)[0], 0);
			EXPECT_EQ((Collection->TransformToGeometryIndex)[1], 1);
			EXPECT_EQ((Collection->TransformToGeometryIndex)[2], 2);
			EXPECT_EQ((Collection->TransformToGeometryIndex)[3], 3);
			EXPECT_EQ((Collection->TransformToGeometryIndex)[4], 4);
			EXPECT_EQ((Collection->TransformToGeometryIndex)[5], 5);

			EXPECT_EQ((Collection->FaceStart)[0], 0);
			EXPECT_EQ((Collection->FaceStart)[1], 12);
			EXPECT_EQ((Collection->FaceStart)[2], 24);
			EXPECT_EQ((Collection->FaceStart)[3], 36);
			EXPECT_EQ((Collection->FaceStart)[4], 48);
			EXPECT_EQ((Collection->FaceStart)[5], 60);

			EXPECT_EQ((Collection->FaceCount)[0], 12);
			EXPECT_EQ((Collection->FaceCount)[1], 12);
			EXPECT_EQ((Collection->FaceCount)[1], 12);
			EXPECT_EQ((Collection->FaceCount)[1], 12);
			EXPECT_EQ((Collection->FaceCount)[1], 12);
			EXPECT_EQ((Collection->FaceCount)[1], 12);
			EXPECT_EQ((Collection->Indices).Num(), 72);

			EXPECT_EQ((Collection->VertexStart)[0], 0);
			EXPECT_EQ((Collection->VertexStart)[1], 8);
			EXPECT_EQ((Collection->VertexStart)[2], 16);
			EXPECT_EQ((Collection->VertexStart)[3], 24);
			EXPECT_EQ((Collection->VertexStart)[4], 32);
			EXPECT_EQ((Collection->VertexStart)[5], 40);

			EXPECT_EQ((Collection->VertexCount)[0], 8);
			EXPECT_EQ((Collection->VertexCount)[1], 8);
			EXPECT_EQ((Collection->VertexCount)[2], 8);
			EXPECT_EQ((Collection->VertexCount)[3], 8);
			EXPECT_EQ((Collection->VertexCount)[4], 8);
			EXPECT_EQ((Collection->VertexCount)[5], 8);
			EXPECT_EQ((Collection->Vertex).Num(), 48);
		}

		EXPECT_TRUE(Collection->HasContiguousFaces());
		EXPECT_TRUE(Collection->HasContiguousVertices());
		EXPECT_TRUE(Collection->HasContiguousRenderFaces());
		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));
	}
	template void AppendTransformHierarchy<float>();

	template<class T>
	void ContiguousElementsTest()
	{
		{
			TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
			EXPECT_TRUE(Collection->HasContiguousFaces());
			EXPECT_TRUE(Collection->HasContiguousVertices());
			Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
			EXPECT_TRUE(Collection->HasContiguousFaces());
			EXPECT_TRUE(Collection->HasContiguousVertices());
			Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
			EXPECT_TRUE(Collection->HasContiguousFaces());
			EXPECT_TRUE(Collection->HasContiguousVertices());
		}
		{
			TSharedPtr<FGeometryCollection> Collection(new FGeometryCollection());
			GeometryCollection::SetupCubeGridExample(Collection);
			EXPECT_TRUE(Collection->HasContiguousFaces());
			EXPECT_TRUE(Collection->HasContiguousVertices());
		}
	}
	template void ContiguousElementsTest<float>();

	template<class T>
	void DeleteFromEnd()
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 20)), FVector(1.0)));

		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 1;

		EXPECT_TRUE(Collection->HasGroup(FTransformCollection::TransformGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::VerticesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::FacesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::MaterialGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::GeometryGroup));

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 3);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 24);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 36);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 3);

		int HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 3);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[2], 2);

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));

		TArray<int32> DelList = { 2 };
		Collection->RemoveElements(FTransformCollection::TransformGroup, DelList);

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 16);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 24);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 2);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);

		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::VerticesGroup); Index++)
		{
			EXPECT_TRUE((Collection->BoneMap)[Index] < Collection->NumElements(FGeometryCollection::TransformGroup));
		}
		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::FacesGroup); Index++)
		{
			EXPECT_TRUE((Collection->Indices)[Index][0] < Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_TRUE((Collection->Indices)[Index][1] < Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_TRUE((Collection->Indices)[Index][2] < Collection->NumElements(FGeometryCollection::VerticesGroup));
		}

		EXPECT_EQ((Collection->Transform)[0].GetTranslation().Z, 0.f);
		EXPECT_EQ((Collection->Transform)[1].GetTranslation().Z, 10.f);

		HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		// GeometryGroup Updated Tests
		{
			EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 2);

			EXPECT_EQ((Collection->TransformIndex)[0], 0);
			EXPECT_EQ((Collection->TransformIndex)[1], 1);

			EXPECT_EQ((Collection->FaceStart)[0], 0);
			EXPECT_EQ((Collection->FaceStart)[1], 12);

			EXPECT_EQ((Collection->FaceCount)[0], 12);
			EXPECT_EQ((Collection->FaceCount)[1], 12);
			EXPECT_EQ((Collection->Indices).Num(), 24);

			EXPECT_EQ((Collection->VertexStart)[0], 0);
			EXPECT_EQ((Collection->VertexStart)[1], 8);

			EXPECT_EQ((Collection->VertexCount)[0], 8);
			EXPECT_EQ((Collection->VertexCount)[1], 8);
			EXPECT_EQ((Collection->Vertex).Num(), 16);
		}

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));
		EXPECT_TRUE(Collection->HasContiguousFaces());
		EXPECT_TRUE(Collection->HasContiguousVertices());
		EXPECT_TRUE(Collection->HasContiguousRenderFaces());
	}
	template void DeleteFromEnd<float>();


	template<class T>
	void DeleteFromStart()
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 20)), FVector(1.0)));

		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 1;

		EXPECT_TRUE(Collection->HasGroup(FTransformCollection::TransformGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::VerticesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::FacesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::MaterialGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::GeometryGroup));

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 3);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 24);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 36);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 3);

		int HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 3);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[2], 2);

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));

		TArray<int32> DelList = { 0 };
		Collection->RemoveElements(FTransformCollection::TransformGroup, DelList);

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 16);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 24);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 2);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);

		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::VerticesGroup); Index++)
		{
			EXPECT_LT((Collection->BoneMap)[Index], Collection->NumElements(FGeometryCollection::TransformGroup));
		}
		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::FacesGroup); Index++)
		{
			EXPECT_LT((Collection->Indices)[Index][0], Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_LT((Collection->Indices)[Index][1], Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_LT((Collection->Indices)[Index][2], Collection->NumElements(FGeometryCollection::VerticesGroup));
		}

		EXPECT_EQ((Collection->Transform)[0].GetTranslation().Z, 10.f);
		EXPECT_EQ((Collection->Transform)[1].GetTranslation().Z, 20.f);

		HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		// GeometryGroup Updated Tests
		{
			EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 2);

			EXPECT_EQ((Collection->TransformIndex)[0], 0);
			EXPECT_EQ((Collection->TransformIndex)[1], 1);

			EXPECT_EQ((Collection->FaceStart)[0], 0);
			EXPECT_EQ((Collection->FaceStart)[1], 12);

			EXPECT_EQ((Collection->FaceCount)[0], 12);
			EXPECT_EQ((Collection->FaceCount)[1], 12);
			EXPECT_EQ((Collection->Indices).Num(), 24);

			EXPECT_EQ((Collection->VertexStart)[0], 0);
			EXPECT_EQ((Collection->VertexStart)[1], 8);

			EXPECT_EQ((Collection->VertexCount)[0], 8);
			EXPECT_EQ((Collection->VertexCount)[1], 8);
			EXPECT_EQ((Collection->Vertex).Num(), 16);
		}

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));
		EXPECT_TRUE(Collection->HasContiguousFaces());
		EXPECT_TRUE(Collection->HasContiguousVertices());
		EXPECT_TRUE(Collection->HasContiguousRenderFaces());
	}
	template void DeleteFromStart<float>();

	template<class T>
	void DeleteFromMiddle()
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 20)), FVector(1.0)));

		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);

		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(2);

		(Collection->Parent)[2] = 1;

		EXPECT_TRUE(Collection->HasGroup(FTransformCollection::TransformGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::VerticesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::FacesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::MaterialGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::GeometryGroup));

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 3);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 24);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 36);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 3);

		int HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 3);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[2], 2);

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));

		TArray<int32> DelList = { 1 };
		Collection->RemoveElements(FTransformCollection::TransformGroup, DelList);

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 16);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 24);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 2);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);

		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::VerticesGroup); Index++)
		{
			EXPECT_LT((Collection->BoneMap)[Index], Collection->NumElements(FGeometryCollection::TransformGroup));
		}
		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::FacesGroup); Index++)
		{
			EXPECT_LT((Collection->Indices)[Index][0], Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_LT((Collection->Indices)[Index][1], Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_LT((Collection->Indices)[Index][2], Collection->NumElements(FGeometryCollection::VerticesGroup));
		}

		EXPECT_EQ((Collection->Transform)[0].GetTranslation().Z, 0.f);
		EXPECT_EQ((Collection->Transform)[1].GetTranslation().Z, 30.f);

		HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		// GeometryGroup Updated Tests
		{
			EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 2);

			EXPECT_EQ((Collection->TransformIndex)[0], 0);
			EXPECT_EQ((Collection->TransformIndex)[1], 1);

			EXPECT_EQ((Collection->FaceStart)[0], 0);
			EXPECT_EQ((Collection->FaceStart)[1], 12);

			EXPECT_EQ((Collection->FaceCount)[0], 12);
			EXPECT_EQ((Collection->FaceCount)[1], 12);
			EXPECT_EQ((Collection->Indices).Num(), 24);

			EXPECT_EQ((Collection->VertexStart)[0], 0);
			EXPECT_EQ((Collection->VertexStart)[1], 8);

			EXPECT_EQ((Collection->VertexCount)[0], 8);
			EXPECT_EQ((Collection->VertexCount)[1], 8);
			EXPECT_EQ((Collection->Vertex).Num(), 16);
		}

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));
		EXPECT_TRUE(Collection->HasContiguousFaces());
		EXPECT_TRUE(Collection->HasContiguousVertices());
		EXPECT_TRUE(Collection->HasContiguousRenderFaces());
	}
	template void DeleteFromMiddle<float>();


	template<class T>
	void DeleteBranch()
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));

		//  0
		//  ...1
		//  ......3
		//  ...2
		//  ......4
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);
		(Collection->Children)[0].Add(2);
		(Collection->Parent)[1] = 0;
		(Collection->Children)[1].Add(3);
		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(4);
		(Collection->Parent)[3] = 1;
		(Collection->Parent)[4] = 2;

		EXPECT_TRUE(Collection->HasGroup(FTransformCollection::TransformGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::VerticesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::FacesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::MaterialGroup));

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 5);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 40);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 60);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 5);

		int HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 5);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[2], 2);
		EXPECT_EQ(Collection->TransformToGeometryIndex[3], 3);
		EXPECT_EQ(Collection->TransformToGeometryIndex[4], 4);

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));

		//  0
		//  ...2
		//  ......4
		TArray<int32> DelList = { 1, 3 };
		Collection->RemoveElements(FTransformCollection::TransformGroup, DelList);

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 3);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 24);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 36);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);

		EXPECT_EQ((Collection->Parent)[0], -1);
		EXPECT_EQ((Collection->Children)[0].Num(), 1);
		EXPECT_TRUE((Collection->Children)[0].Contains(1));
		EXPECT_EQ((Collection->Parent)[1], 0);
		EXPECT_EQ((Collection->Children)[1].Num(), 1);
		EXPECT_TRUE((Collection->Children)[1].Contains(2));
		EXPECT_EQ((Collection->Parent)[2], 1);
		EXPECT_EQ((Collection->Children)[2].Num(), 0);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 3);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[2], 2);

		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::VerticesGroup); Index++)
		{
			EXPECT_LT((Collection->BoneMap)[Index], Collection->NumElements(FGeometryCollection::TransformGroup));
		}
		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::FacesGroup); Index++)
		{
			EXPECT_LT((Collection->Indices)[Index][0], Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_LT((Collection->Indices)[Index][1], Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_LT((Collection->Indices)[Index][2], Collection->NumElements(FGeometryCollection::VerticesGroup));
		}

		EXPECT_EQ((Collection->Transform)[0].GetTranslation().Z, 0.f);
		EXPECT_EQ((Collection->Transform)[1].GetTranslation().Z, 10.f);

		HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		// GeometryGroup Updated Tests
		{
			EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 3);

			EXPECT_EQ((Collection->TransformIndex)[0], 0);
			EXPECT_EQ((Collection->TransformIndex)[1], 1);
			EXPECT_EQ((Collection->TransformIndex)[2], 2);

			EXPECT_EQ((Collection->FaceStart)[0], 0);
			EXPECT_EQ((Collection->FaceStart)[1], 12);
			EXPECT_EQ((Collection->FaceStart)[2], 24);

			EXPECT_EQ((Collection->FaceCount)[0], 12);
			EXPECT_EQ((Collection->FaceCount)[1], 12);
			EXPECT_EQ((Collection->FaceCount)[2], 12);
			EXPECT_EQ((Collection->Indices).Num(), 36);

			EXPECT_EQ((Collection->VertexStart)[0], 0);
			EXPECT_EQ((Collection->VertexStart)[1], 8);
			EXPECT_EQ((Collection->VertexStart)[2], 16);

			EXPECT_EQ((Collection->VertexCount)[0], 8);
			EXPECT_EQ((Collection->VertexCount)[1], 8);
			EXPECT_EQ((Collection->VertexCount)[2], 8);
			EXPECT_EQ((Collection->Vertex).Num(), 24);
		}

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));
		EXPECT_TRUE(Collection->HasContiguousFaces());
		EXPECT_TRUE(Collection->HasContiguousVertices());
		EXPECT_TRUE(Collection->HasContiguousRenderFaces());
	}
	template void DeleteBranch<float>();


	template<class T>
	void DeleteRootLeafMiddle()
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));

		//  0
		//  ...1
		//  ...5
		//  ......6
		//  ......3
		//  ...2
		//  ......7
		//  .........4
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);
		(Collection->Children)[0].Add(5);
		(Collection->Children)[0].Add(2);
		(Collection->Parent)[1] = 0;
		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(7);
		(Collection->Parent)[3] = 5;
		(Collection->Parent)[4] = 7;
		(Collection->Parent)[5] = 0;
		(Collection->Children)[5].Add(6);
		(Collection->Children)[5].Add(3);
		(Collection->Parent)[6] = 5;
		(Collection->Parent)[7] = 2;
		(Collection->Children)[7].Add(4);

		(Collection->BoneName)[0] = "0";
		(Collection->BoneName)[1] = "1";
		(Collection->BoneName)[2] = "2";
		(Collection->BoneName)[3] = "3";
		(Collection->BoneName)[4] = "4";
		(Collection->BoneName)[5] = "5";
		(Collection->BoneName)[6] = "6";
		(Collection->BoneName)[7] = "7";


		EXPECT_TRUE(Collection->HasGroup(FTransformCollection::TransformGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::VerticesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::FacesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::MaterialGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::GeometryGroup));

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 8);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 64);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 96);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 8);

		int HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 8);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[2], 2);
		EXPECT_EQ(Collection->TransformToGeometryIndex[3], 3);
		EXPECT_EQ(Collection->TransformToGeometryIndex[4], 4);
		EXPECT_EQ(Collection->TransformToGeometryIndex[5], 5);
		EXPECT_EQ(Collection->TransformToGeometryIndex[6], 6);
		EXPECT_EQ(Collection->TransformToGeometryIndex[7], 7);

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));

		//  1
		//  6
		//  3
		//  2
		//  ...4
		TArray<int32> DelList = { 0,5,7 };
		Collection->RemoveElements(FTransformCollection::TransformGroup, DelList);

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 5);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 40);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 60);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 2);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 5);

		EXPECT_EQ((Collection->Parent)[0], -1);
		EXPECT_EQ((Collection->Children)[0].Num(), 0);
		EXPECT_EQ((Collection->Parent)[1], -1);
		EXPECT_EQ((Collection->Children)[1].Num(), 1);
		EXPECT_TRUE((Collection->Children)[1].Contains(3));
		EXPECT_EQ((Collection->Parent)[2], -1);
		EXPECT_EQ((Collection->Children)[2].Num(), 0);
		EXPECT_EQ((Collection->Parent)[3], 1);
		EXPECT_EQ((Collection->Children)[3].Num(), 0);
		EXPECT_EQ((Collection->Parent)[4], -1);
		EXPECT_EQ((Collection->Children)[4].Num(), 0);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 5);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[2], 2);
		EXPECT_EQ(Collection->TransformToGeometryIndex[3], 3);
		EXPECT_EQ(Collection->TransformToGeometryIndex[4], 4);

		int32 Index0 = Collection->BoneName.Find("0");
		int32 Index1 = Collection->BoneName.Find("1");
		int32 Index2 = Collection->BoneName.Find("2");
		int32 Index3 = Collection->BoneName.Find("3");
		int32 Index4 = Collection->BoneName.Find("4");
		int32 Index6 = Collection->BoneName.Find("6");

		EXPECT_EQ(Index0, -1);
		EXPECT_NE(Index6, -1);
		EXPECT_EQ((Collection->Parent)[Index1], -1);
		EXPECT_EQ((Collection->Parent)[Index2], -1);
		EXPECT_EQ((Collection->Children)[Index2].Num(), 1);
		EXPECT_TRUE((Collection->Children)[Index2].Contains(Index4));
		EXPECT_EQ((Collection->Parent)[Index4], Index2);
		EXPECT_EQ((Collection->Children)[Index4].Num(), 0);

		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::VerticesGroup); Index++)
		{
			EXPECT_LT((Collection->BoneMap)[Index], Collection->NumElements(FGeometryCollection::TransformGroup));
		}
		for (int Index = 0; Index < Collection->NumElements(FGeometryCollection::FacesGroup); Index++)
		{
			EXPECT_LT((Collection->Indices)[Index][0], Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_LT((Collection->Indices)[Index][1], Collection->NumElements(FGeometryCollection::VerticesGroup));
			EXPECT_LT((Collection->Indices)[Index][2], Collection->NumElements(FGeometryCollection::VerticesGroup));
		}

		EXPECT_EQ((Collection->Transform)[Index1].GetTranslation().Z, 10.f);
		EXPECT_EQ((Collection->Transform)[Index2].GetTranslation().Z, 10.f);
		EXPECT_EQ((Collection->Transform)[Index3].GetTranslation().Z, 20.f);
		EXPECT_EQ((Collection->Transform)[Index4].GetTranslation().Z, 20.f);
		EXPECT_EQ((Collection->Transform)[Index6].GetTranslation().Z, 20.f);

		HalfTheFaces = Collection->NumElements(FGeometryCollection::FacesGroup) / 2;
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].FirstIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[0].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[0].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);

		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].FirstIndex, HalfTheFaces * 3);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, HalfTheFaces);
		EXPECT_EQ((Collection->Sections)[1].MinVertexIndex, 0);
		EXPECT_EQ((Collection->Sections)[1].MaxVertexIndex, Collection->NumElements(FGeometryCollection::VerticesGroup) - 1);


		// GeometryGroup Updated Tests
		{
			EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 5);


			EXPECT_EQ((Collection->TransformIndex)[Index1], 0);
			EXPECT_EQ((Collection->TransformIndex)[Index2], 1);
			EXPECT_EQ((Collection->TransformIndex)[Index3], 2);
			EXPECT_EQ((Collection->TransformIndex)[Index4], 3);
			EXPECT_EQ((Collection->TransformIndex)[Index6], 4);

			EXPECT_EQ((Collection->FaceStart)[Index1], 0);
			EXPECT_EQ((Collection->FaceStart)[Index2], 12);
			EXPECT_EQ((Collection->FaceStart)[Index3], 24);
			EXPECT_EQ((Collection->FaceStart)[Index4], 36);
			EXPECT_EQ((Collection->FaceStart)[Index6], 48);

			EXPECT_EQ((Collection->FaceCount)[Index1], 12);
			EXPECT_EQ((Collection->FaceCount)[Index2], 12);
			EXPECT_EQ((Collection->FaceCount)[Index3], 12);
			EXPECT_EQ((Collection->FaceCount)[Index4], 12);
			EXPECT_EQ((Collection->FaceCount)[Index6], 12);
			EXPECT_EQ((Collection->Indices).Num(), 60);

			EXPECT_EQ((Collection->VertexStart)[Index1], 0);
			EXPECT_EQ((Collection->VertexStart)[Index2], 8);
			EXPECT_EQ((Collection->VertexStart)[Index3], 16);
			EXPECT_EQ((Collection->VertexStart)[Index4], 24);
			EXPECT_EQ((Collection->VertexStart)[Index6], 32);

			EXPECT_EQ((Collection->VertexCount)[Index1], 8);
			EXPECT_EQ((Collection->VertexCount)[Index2], 8);
			EXPECT_EQ((Collection->VertexCount)[Index3], 8);
			EXPECT_EQ((Collection->VertexCount)[Index4], 8);
			EXPECT_EQ((Collection->VertexCount)[Index6], 8);
			EXPECT_EQ((Collection->Vertex).Num(), 40);
		}

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));
		EXPECT_TRUE(Collection->HasContiguousFaces());
		EXPECT_TRUE(Collection->HasContiguousVertices());
		EXPECT_TRUE(Collection->HasContiguousRenderFaces());
	}
	template void DeleteRootLeafMiddle<float>();


	template<class T>
	void DeleteEverything()
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));
		Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 10)), FVector(1.0)));

		//  0
		//  ...1
		//  ...5
		//  ......6
		//  ......3
		//  ...2
		//  ......7
		//  .........4
		(Collection->Parent)[0] = -1;
		(Collection->Children)[0].Add(1);
		(Collection->Children)[0].Add(5);
		(Collection->Children)[0].Add(2);
		(Collection->Parent)[1] = 0;
		(Collection->Parent)[2] = 0;
		(Collection->Children)[2].Add(7);
		(Collection->Parent)[3] = 5;
		(Collection->Parent)[4] = 7;
		(Collection->Parent)[5] = 0;
		(Collection->Children)[5].Add(6);
		(Collection->Children)[5].Add(3);
		(Collection->Parent)[6] = 5;
		(Collection->Parent)[7] = 2;
		(Collection->Children)[7].Add(4);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 8);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], 0);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], 1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[2], 2);
		EXPECT_EQ(Collection->TransformToGeometryIndex[3], 3);
		EXPECT_EQ(Collection->TransformToGeometryIndex[4], 4);
		EXPECT_EQ(Collection->TransformToGeometryIndex[5], 5);
		EXPECT_EQ(Collection->TransformToGeometryIndex[6], 6);
		EXPECT_EQ(Collection->TransformToGeometryIndex[7], 7);

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));

		TArray<int32> DelList = { 0,1,2,3,4,5,6,7 };
		Collection->RemoveElements(FTransformCollection::TransformGroup, DelList);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 0);

		EXPECT_TRUE(Collection->HasGroup(FTransformCollection::TransformGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::VerticesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::FacesGroup));
		EXPECT_TRUE(Collection->HasGroup(FGeometryCollection::MaterialGroup));

		EXPECT_EQ(Collection->NumElements(FTransformCollection::TransformGroup), 0);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::VerticesGroup), 0);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::FacesGroup), 0);
		EXPECT_EQ(Collection->NumElements(FGeometryCollection::MaterialGroup), 0);

		EXPECT_EQ(Collection->NumElements(FGeometryCollection::GeometryGroup), 0);
		EXPECT_EQ((Collection->Indices).Num(), 0);
		EXPECT_EQ((Collection->Vertex).Num(), 0);

		EXPECT_TRUE(GeometryCollectionAlgo::HasValidGeometryReferences(Collection.Get()));
		EXPECT_TRUE(Collection->HasContiguousFaces());
		EXPECT_TRUE(Collection->HasContiguousVertices());
		EXPECT_TRUE(Collection->HasContiguousRenderFaces());
	}
	template void DeleteEverything<float>();


	template<class T>
	void ParentTransformTest()
	{
		FGeometryCollection* Collection = new FGeometryCollection();

		int32 TransformIndex = Collection->AddElements(1, FGeometryCollection::TransformGroup);
		(Collection->Transform)[TransformIndex].SetTranslation(FVector(13));
		(Collection->Parent)[TransformIndex]= -1;
		EXPECT_EQ(TransformIndex, 0);

		TransformIndex = Collection->AddElements(1, FGeometryCollection::TransformGroup);
		(Collection->Transform)[TransformIndex].SetTranslation(FVector(7));
		(Collection->Parent)[TransformIndex] = -1;
		EXPECT_EQ(TransformIndex, 1);

		//
		// Parent a transform
		//
		GeometryCollectionAlgo::ParentTransform(Collection, 1, 0);
		EXPECT_EQ((Collection->Children)[0].Num(), 0);
		EXPECT_EQ((Collection->Parent)[0], 1);
		EXPECT_EQ((Collection->Children)[1].Num(), 1);
		EXPECT_TRUE((Collection->Children)[1].Contains(0));
		EXPECT_EQ((Collection->Parent)[1], -1);

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);
		EXPECT_LT(((Collection->Transform)[0].GetTranslation() - FVector(6)).Size(), KINDA_SMALL_NUMBER);
		EXPECT_LT((GlobalTransform[0].GetTranslation()-FVector(13)).Size(), KINDA_SMALL_NUMBER);

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 2);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], -1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], -1);

		//
		// Add some geometry
		//
		TransformIndex = Collection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(3)), FVector(1.0)));
		EXPECT_LT(((Collection->Transform)[TransformIndex].GetTranslation() - FVector(3)).Size(), KINDA_SMALL_NUMBER);
		EXPECT_EQ((Collection->TransformIndex).Num(), 1);
		EXPECT_EQ((Collection->TransformIndex)[0], TransformIndex);
		EXPECT_EQ((Collection->VertexStart)[0], 0);
		EXPECT_EQ((Collection->VertexCount)[0], 8);
		for (int i = (Collection->VertexStart)[0]; i < (Collection->VertexCount)[0]; i++)
		{
			EXPECT_EQ((Collection->BoneMap)[i], TransformIndex);
		}

		EXPECT_EQ(Collection->TransformToGeometryIndex.Num(), 3);
		EXPECT_EQ(Collection->TransformToGeometryIndex[0], -1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[1], -1);
		EXPECT_EQ(Collection->TransformToGeometryIndex[2], 0);

		//
		// Parent the geometry
		//
		GeometryCollectionAlgo::ParentTransform(Collection, 0, TransformIndex);
		EXPECT_EQ((Collection->Children)[0].Num(), 1);
		EXPECT_EQ((Collection->Parent)[0], 1);
		EXPECT_EQ((Collection->Children)[1].Num(), 1);
		EXPECT_TRUE((Collection->Children)[1].Contains(0));
		EXPECT_EQ((Collection->Parent)[1], -1);
		EXPECT_LT(((Collection->Transform)[TransformIndex].GetTranslation() - FVector(-10)).Size(), KINDA_SMALL_NUMBER);
		EXPECT_EQ((Collection->TransformIndex).Num(), 1);
		EXPECT_EQ((Collection->TransformIndex)[0], TransformIndex);
		EXPECT_EQ((Collection->VertexStart)[0], 0);
		EXPECT_EQ((Collection->VertexCount)[0], 8);
		for (int i = (Collection->VertexStart)[0]; i < (Collection->VertexCount)[0]; i++)
		{
			EXPECT_EQ((Collection->BoneMap)[i], TransformIndex);
		}

		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransform);
		EXPECT_LT((GlobalTransform[0].GetTranslation() - FVector(13)).Size(), KINDA_SMALL_NUMBER);
		EXPECT_LT((GlobalTransform[2].GetTranslation() - FVector(3)).Size(), KINDA_SMALL_NUMBER);


		//
		// Force a circular parent
		//
		EXPECT_FALSE(GeometryCollectionAlgo::HasCycle((Collection->Parent), TransformIndex));
		(Collection->Children)[0].Add(2);
		(Collection->Parent)[0] = 2;
		(Collection->Children)[2].Add(0);
		(Collection->Parent)[2] = 0;
		EXPECT_TRUE(GeometryCollectionAlgo::HasCycle((Collection->Parent), TransformIndex));

		delete Collection;
	}
	template void ParentTransformTest<float>();

	template<class T>
	void ReindexMaterialsTest()
	{
		TSharedPtr<FGeometryCollection> Collection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		
		EXPECT_EQ(Collection->Sections.Num(), 2);

		Collection->ReindexMaterials();

		// Reindexing doesn't change the number of sections
		EXPECT_EQ(Collection->Sections.Num(), 2);

		// Ensure material selections have correct material ids after reindexing
		for (int i = 0; i < 12; i++)
		{
			if (i < 6)
			{
				EXPECT_EQ((Collection->MaterialID)[i], 0);
			}
			else
			{
				EXPECT_EQ((Collection->MaterialID)[i], 1);
			}
		}

		// Delete vertices for a single material id
		TArray<int32> DelList = { 0,1,2,3,4,5 };
		Collection->RemoveElements(FGeometryCollection::FacesGroup, DelList);

		Collection->ReindexMaterials();

		// Ensure we now have 1 section
		EXPECT_EQ(Collection->Sections.Num(), 1);
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, 6);		
		 
		// Add a copy of the geometry and reindex
		TSharedPtr<FGeometryCollection> Collection2 = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		Collection->AppendGeometry(*Collection2.Get());
		Collection->ReindexMaterials();

		// test that sections created are consolidated
		EXPECT_EQ(Collection->Sections.Num(), 2);
		EXPECT_EQ((Collection->Sections)[0].MaterialID, 0);
		EXPECT_EQ((Collection->Sections)[0].NumTriangles, 6);
		EXPECT_EQ((Collection->Sections)[1].MaterialID, 1);
		EXPECT_EQ((Collection->Sections)[1].NumTriangles, 12);
	}
	template void ReindexMaterialsTest<float>();

	template<class T>
	void AttributeTransferTest()
	{
		TSharedPtr<FGeometryCollection> Collection1 = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TSharedPtr<FGeometryCollection> Collection2 = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TSharedPtr<FGeometryCollection> Collection3 = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(2.0));
		Collection2->AppendGeometry(*Collection3.Get());
		// set color one 1
		for (int i = 0; i < Collection1->NumElements(FGeometryCollection::VerticesGroup); ++i)
		{
			(Collection1->Color)[i] = FLinearColor(1, 0, 1, 1);
		}

		// transfer color to 2
		FName Attr("Color");
		GeometryCollection::AttributeTransfer<FLinearColor>(Collection1.Get(), Collection2.Get(), Attr, Attr);

		// test color is set correctly on 2
		for (int i = 0; i < Collection2->NumElements(FGeometryCollection::VerticesGroup); ++i)
		{
			EXPECT_TRUE((Collection2->Color)[i].Equals(FColor(1, 0, 1, 1)));
		}
	}
	template void AttributeTransferTest<float>();

	template<class T>
	void AttributeDependencyTest()
	{
		FGeometryCollection* Collection = new FGeometryCollection();

		TManagedArray<FTransform> Transform;
		TManagedArray<int32> Int32s;

		FName Group1 = "Group1";
		FName Group2 = "Group2";
		FName Group3 = "Group3";
		FName Group4 = "Group4";
		FName Group5 = "Group5";

		FManagedArrayCollection::FConstructionParameters TransformDependency(FTransformCollection::TransformGroup);

		// valid dependency
		// (A)G1
		// |
		// _______
		// |      |
		// (B)G2  (D)G4
		// |
		// (C)G3
		Collection->AddExternalAttribute<FTransform>("AttributeA", Group1, Transform);
		Collection->AddExternalAttribute<FTransform>("AttributeB", Group2, Transform, Group1);
		Collection->AddExternalAttribute<FTransform>("AttributeC", Group3, Transform, Group2);
		Collection->AddExternalAttribute<FTransform>("AttributeD", Group4, Transform, Group1);

		// Force a circular group dependency - from G1 to G3
		// can't figure how to trap the assert that is deliberately fired off during this call
		//Collection->SetDependency()<FTransform>("AttributeD", Group1, Transform, Group3);

		delete Collection;
	}
	template void AttributeDependencyTest<float>();

}
