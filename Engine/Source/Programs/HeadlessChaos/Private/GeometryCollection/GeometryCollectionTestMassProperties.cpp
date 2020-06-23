// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionTestMassProperties.h"

#include "Chaos/TriangleMesh.h"
#include "Chaos/MassProperties.h"
#include "Math/Box.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionTestFramework.h"
#include "GeometryCollection/GeometryCollectionTestUtility.h"
#include "GeometryCollection/TransformCollection.h"
#include "HeadlessChaosTestUtility.h"

namespace GeometryCollectionTest
{
	using namespace ChaosTest;

	TYPED_TEST(AllTraits, GeometryCollection_MassProperties_Compute)
	{
		using Traits = TypeParam;
		using namespace Chaos;

		Chaos::TParticles<float, 3> Vertices;
		Vertices.AddParticles(8);
		Vertices.X(0) = TVector<float, 3>(-1, 1, -1);
		Vertices.X(1) = TVector<float, 3>(1, 1, -1);
		Vertices.X(2) = TVector<float, 3>(1, -1, -1);
		Vertices.X(3) = TVector<float, 3>(-1, -1, -1);
		Vertices.X(4) = TVector<float, 3>(-1, 1, 1);
		Vertices.X(5) = TVector<float, 3>(1, 1, 1);
		Vertices.X(6) = TVector<float, 3>(1, -1, 1);
		Vertices.X(7) = TVector<float, 3>(-1, -1, 1);

		// @todo(chaos):  breaking : this trips an ensure in the test, why?
		for (int i = 0; i < 8; i++) {
			Vertices.X(i) *= FVector(1, 2, 3);
			Vertices.X(i) += FVector(1, 2, 3);
		}

		TArray<Chaos::TVector<int32, 3>> Faces;
		Faces.SetNum(12);
		Faces[0] = TVector<int32, 3>(0,1,2);
		Faces[1] = TVector<int32, 3>(0,2,3);
		Faces[2] = TVector<int32, 3>(2,1,6);
		Faces[3] = TVector<int32, 3>(1,5,6);
		Faces[4] = TVector<int32, 3>(2,6,7);
		Faces[5] = TVector<int32, 3>(3,2,7);
		Faces[6] = TVector<int32, 3>(4,7,3);
		Faces[7] = TVector<int32, 3>(4,0,3);
		Faces[8] = TVector<int32, 3>(4,1,0);
		Faces[9] = TVector<int32, 3>(4,5,1);
		Faces[10] = TVector<int32, 3>(5,4,7);
		Faces[11] = TVector<int32, 3>(5,7,6);
		Chaos::TTriangleMesh<float> Surface(MoveTemp(Faces));

		TMassProperties<FReal, 3> MassProperties;
		MassProperties.Mass = 1.f;
		//Chaos::TMassProperties<float, 3> MassProperties = Chaos::CalculateMassProperties(Vertices, Surface.GetElements(), 1.f);
		{
			const auto& SurfaceElements = Surface.GetElements();
			CalculateVolumeAndCenterOfMass(Vertices, SurfaceElements, MassProperties.Volume, MassProperties.CenterOfMass);

			for (int32 Idx = 0; Idx < 8; ++Idx)
			{
				Vertices.X(Idx) -= MassProperties.CenterOfMass;
			}

			check(MassProperties.Mass > 0);
			check(MassProperties.Volume > SMALL_NUMBER);
			
			// @todo(chaos) : breaking
			CalculateInertiaAndRotationOfMass(Vertices, SurfaceElements, MassProperties.Mass / MassProperties.Volume, MassProperties.CenterOfMass, MassProperties.InertiaTensor, MassProperties.RotationOfMass);
		}
		EXPECT_EQ(MassProperties.Mass, 1.f);
		EXPECT_TRUE(MassProperties.CenterOfMass.Equals(FVector(1, 2, 3)));
		
		// This is just measured data to let us know when it changes. Ideally this would be derived. 
		FVector EulerAngle = MassProperties.RotationOfMass.Euler();
		EXPECT_TRUE(MassProperties.RotationOfMass.Euler().Equals(FVector(115.8153, -12.4347, 1.9705)));
		EXPECT_TRUE(FMath::IsNearlyEqual(MassProperties.InertiaTensor.M[0][0], static_cast<FReal>(14.9866095), KINDA_SMALL_NUMBER));
		EXPECT_TRUE(FMath::IsNearlyEqual(MassProperties.InertiaTensor.M[1][1], static_cast<FReal>(1.40656376), KINDA_SMALL_NUMBER));
		EXPECT_TRUE(FMath::IsNearlyEqual(MassProperties.InertiaTensor.M[2][2], static_cast<FReal>(13.7401619), KINDA_SMALL_NUMBER));
	}

	TYPED_TEST(AllTraits, GeometryCollection_MassProperties_Cube)
	{
		using Traits = TypeParam;
		using namespace Chaos;
		FVector GlobalTranslation(0); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
		CreationParameters Params; Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Box; Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		Params.GeomTransform = FTransform(GlobalRotation, GlobalTranslation); Params.NestedTransforms = { FTransform::Identity, FTransform::Identity,  FTransform::Identity };
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		TArray<FTransform> Transform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->RestCollection->Transform, Collection->RestCollection->Parent, Transform);

		// group ?
		const TManagedArray<bool>& Visible = Collection->RestCollection->Visible;

		// VerticesGroup
		const TManagedArray<FVector>& Vertex = Collection->RestCollection->Vertex;

		// GeometryGroup
		const int32 NumGeometries = Collection->RestCollection->NumElements(FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& VertexCount = Collection->RestCollection->VertexCount;
		const TManagedArray<int32>& VertexStart = Collection->RestCollection->VertexStart;
		const TManagedArray<int32>& FaceCount = Collection->RestCollection->FaceCount;
		const TManagedArray<int32>& FaceStart = Collection->RestCollection->FaceStart;
		const TManagedArray<int32>& TransformIndex = Collection->RestCollection->TransformIndex;
		const TManagedArray<FIntVector>& Indices = Collection->RestCollection->Indices;
		const TManagedArray<int32>& BoneMap = Collection->RestCollection->BoneMap;
		int GeometryIndex = 0;

		TUniquePtr<TTriangleMesh<float>> TriMesh(
			CreateTriangleMesh(
				FaceStart[GeometryIndex],
				FaceCount[GeometryIndex],
				Visible,
				Indices,
				false));

		//TArray<Chaos::TVector<int32, 3>> Faces;
		//Faces.SetNum(Indices.Num());
		//for (int i = 0; i < Indices.Num(); i++) { Faces[i] = TVector<int32, 3>(Indices[i][0], Indices[i][1], Indices[i][2]); }
		//Chaos::TTriangleMesh<float> TriMesh(MoveTemp(Faces));

		TArray<TMassProperties<float, 3>> MassPropertiesArray;
		MassPropertiesArray.AddUninitialized(NumGeometries);
		TMassProperties<float, 3>& MassProperties = MassPropertiesArray[GeometryIndex];
		MassProperties.CenterOfMass = FVector(0);
		MassProperties.Mass = 1.f;

		TParticles<float, 3> MassSpaceParticles;
		MassSpaceParticles.AddParticles(Vertex.Num());
		for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
		{
			MassSpaceParticles.X(Idx) = Transform[BoneMap[Idx]].TransformPosition(Vertex[Idx]);
		}

		CalculateVolumeAndCenterOfMass(MassSpaceParticles, TriMesh->GetElements(), MassProperties.Volume, MassProperties.CenterOfMass);

		EXPECT_NEAR(MassProperties.Volume - 8.0, 0.0f, KINDA_SMALL_NUMBER); 
		EXPECT_NEAR(MassProperties.CenterOfMass.X - GlobalTranslation[0], 0.0f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.CenterOfMass.Y - GlobalTranslation[1], 0.0f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.CenterOfMass.Z - GlobalTranslation[2], 0.0f, KINDA_SMALL_NUMBER);

		for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
		{
			MassSpaceParticles.X(Idx) -= MassProperties.CenterOfMass;
		}

		float Density = 1.f;
		TVector<float, 3> ZeroVec(0);
		CalculateInertiaAndRotationOfMass(MassSpaceParticles, TriMesh->GetSurfaceElements(), Density, ZeroVec, MassProperties.InertiaTensor, MassProperties.RotationOfMass);

		EXPECT_EQ(MassProperties.Mass, 1.f);
		EXPECT_TRUE((MassProperties.CenterOfMass - GlobalTranslation).Size() < SMALL_NUMBER);

		// This is just measured data to let us know when it changes. Ideally this would be derived. 
		EXPECT_TRUE((MassProperties.RotationOfMass.Euler() - FVector(115.8153, -12.4347, 1.9705)).Size() > KINDA_SMALL_NUMBER);
		EXPECT_TRUE(FMath::IsNearlyEqual(MassProperties.InertiaTensor.M[0][0], static_cast<FReal>(4.99521351), KINDA_SMALL_NUMBER));
		EXPECT_TRUE(FMath::IsNearlyEqual(MassProperties.InertiaTensor.M[1][1], static_cast<FReal>(4.07145357), KINDA_SMALL_NUMBER));
		EXPECT_TRUE(FMath::IsNearlyEqual(MassProperties.InertiaTensor.M[2][2], static_cast<FReal>(4.26666689), KINDA_SMALL_NUMBER));
	}

	TYPED_TEST(AllTraits, GeometryCollection_MassProperties_Sphere)
	{
		using Traits = TypeParam;
		using namespace Chaos;
		FVector GlobalTranslation(10); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0)); FVector Scale(1);
		CreationParameters Params; Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere; Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
		Params.GeomTransform = FTransform(GlobalRotation, GlobalTranslation, Scale); Params.NestedTransforms = { FTransform::Identity, FTransform::Identity,  FTransform::Identity };
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		TArray<FTransform> Transform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->RestCollection->Transform, Collection->RestCollection->Parent, Transform);

		// group ?
		const TManagedArray<bool>& Visible = Collection->RestCollection->Visible;

		// VerticesGroup
		const TManagedArray<FVector>& Vertex = Collection->RestCollection->Vertex;

		// GeometryGroup
		const int32 NumGeometries = Collection->RestCollection->NumElements(FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& VertexCount = Collection->RestCollection->VertexCount;
		const TManagedArray<int32>& VertexStart = Collection->RestCollection->VertexStart;
		const TManagedArray<int32>& FaceCount = Collection->RestCollection->FaceCount;
		const TManagedArray<int32>& FaceStart = Collection->RestCollection->FaceStart;
		const TManagedArray<int32>& TransformIndex = Collection->RestCollection->TransformIndex;
		const TManagedArray<FIntVector>& Indices = Collection->RestCollection->Indices;
		const TManagedArray<int32>& BoneMap = Collection->RestCollection->BoneMap;
		int GeometryIndex = 0;

		TUniquePtr<TTriangleMesh<float>> TriMesh(
			CreateTriangleMesh(
				FaceStart[GeometryIndex],
				FaceCount[GeometryIndex],
				Visible,
				Indices,
				false));

		TArray<TMassProperties<float, 3>> MassPropertiesArray;
		MassPropertiesArray.AddUninitialized(NumGeometries);
		TMassProperties<float, 3>& MassProperties = MassPropertiesArray[GeometryIndex];

		TParticles<float, 3> MassSpaceParticles;
		MassSpaceParticles.AddParticles(Vertex.Num());
		for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
		{
			MassSpaceParticles.X(Idx) = Transform[BoneMap[Idx]].TransformPosition(Vertex[Idx]);
		}

		CalculateVolumeAndCenterOfMass(MassSpaceParticles, TriMesh->GetElements(), MassProperties.Volume, MassProperties.CenterOfMass);

		// Since we're intersecting triangles with a sphere, where the vertices of the 
		// triangle vertices are on the sphere surface, we're missing some volume.  Thus,
		// we'd expect the volume of the triangulation to approach the analytic volume as
		// the number of polygons goes to infinity (MakeSphereElement() currently does 
		// 16x16 divisions in U and V).
		const float AnalyticVolume = (4.0/3) * (22.0/7) * Scale[0] * Scale[0] * Scale[0];
		EXPECT_NEAR(MassProperties.Volume - AnalyticVolume, 0.0f, 0.2); // this should be 4.19047642
		EXPECT_NEAR(MassProperties.CenterOfMass.X - GlobalTranslation[0], 0.0f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.CenterOfMass.Y - GlobalTranslation[1], 0.0f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.CenterOfMass.Z - GlobalTranslation[2], 0.0f, KINDA_SMALL_NUMBER);


		for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
		{
			MassSpaceParticles.X(Idx) -= MassProperties.CenterOfMass;
		}

		float Density = 0.01f;
		TVector<float, 3> ZeroVec(0);
		CalculateInertiaAndRotationOfMass(MassSpaceParticles, TriMesh->GetSurfaceElements(), Density, ZeroVec, MassProperties.InertiaTensor, MassProperties.RotationOfMass);

		// todo(chaos) : Check this. 
		// This is just measured data to let us know when it changes. Ideally this would be derived. 
		FVector EulerAngle = MassProperties.RotationOfMass.Euler();
		//EXPECT_TRUE((MassProperties.RotationOfMass.Euler() - FVector(115.8153, -12.4347, 1.9705)).Size() > SMALL_NUMBER);
		//EXPECT_TRUE(MassProperties.InertiaTensor.M[0][0] - 4.99521351 < SMALL_NUMBER);
		//EXPECT_TRUE(MassProperties.InertiaTensor.M[1][1] - 4.07145357 < SMALL_NUMBER);
		//EXPECT_TRUE(MassProperties.InertiaTensor.M[2][2] - 4.26666689 < SMALL_NUMBER);
	}


	TYPED_TEST(AllTraits, GeometryCollection_MassProperties_Tetrahedron)
	{
		using Traits = TypeParam;
		using namespace Chaos;
		FVector GlobalTranslation(0); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
		CreationParameters Params; Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Tetrahedron; Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
		Params.GeomTransform = FTransform(GlobalRotation, GlobalTranslation); Params.NestedTransforms = { FTransform::Identity, FTransform::Identity,  FTransform::Identity };
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		TArray<FTransform> Transform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->RestCollection->Transform, Collection->RestCollection->Parent, Transform);

		// group ?
		const TManagedArray<bool>& Visible = Collection->RestCollection->Visible;

		// VerticesGroup
		const TManagedArray<FVector>& Vertex = Collection->RestCollection->Vertex;

		// GeometryGroup
		const int32 NumGeometries = Collection->RestCollection->NumElements(FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& VertexCount = Collection->RestCollection->VertexCount;
		const TManagedArray<int32>& VertexStart = Collection->RestCollection->VertexStart;
		const TManagedArray<int32>& FaceCount = Collection->RestCollection->FaceCount;
		const TManagedArray<int32>& FaceStart = Collection->RestCollection->FaceStart;
		const TManagedArray<int32>& TransformIndex = Collection->RestCollection->TransformIndex;
		const TManagedArray<FIntVector>& Indices = Collection->RestCollection->Indices;
		const TManagedArray<int32>& BoneMap = Collection->RestCollection->BoneMap;
		int GeometryIndex = 0;

		TUniquePtr<TTriangleMesh<float>> TriMesh(
			CreateTriangleMesh(
				FaceStart[GeometryIndex],
				FaceCount[GeometryIndex],
				Visible,
				Indices,
				false));

		TArray<TMassProperties<float, 3>> MassPropertiesArray;
		MassPropertiesArray.AddUninitialized(NumGeometries);
		TMassProperties<float, 3>& MassProperties = MassPropertiesArray[GeometryIndex];
		MassProperties.Mass = 1.0f;
		MassProperties.CenterOfMass = FVector(0);

		TParticles<float, 3> MassSpaceParticles;
		MassSpaceParticles.AddParticles(Vertex.Num());
		for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
		{
			MassSpaceParticles.X(Idx) = Transform[BoneMap[Idx]].TransformPosition(Vertex[Idx]);
		}

		CalculateVolumeAndCenterOfMass(MassSpaceParticles, TriMesh->GetElements(), MassProperties.Volume, MassProperties.CenterOfMass);

		EXPECT_NEAR(MassProperties.Volume - 2.666666, 0.0f, KINDA_SMALL_NUMBER); // Tetrahedron with edge lengths 2.8284
		EXPECT_NEAR(MassProperties.CenterOfMass.X - GlobalTranslation[0], 0.0f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.CenterOfMass.Y - GlobalTranslation[1], 0.0f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.CenterOfMass.Z - GlobalTranslation[2], 0.0f, KINDA_SMALL_NUMBER);

		for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
		{
			MassSpaceParticles.X(Idx) -= MassProperties.CenterOfMass;
		}

		float Density = 0.01f;
		TVector<float,3> ZeroVec(0);
		CalculateInertiaAndRotationOfMass(MassSpaceParticles, TriMesh->GetSurfaceElements(), Density, ZeroVec, MassProperties.InertiaTensor, MassProperties.RotationOfMass);

		// todo(chaos) : Check this. 
		// This is just measured data to let us know when it changes. Ideally this would be derived. 
		FVector EulerAngle = MassProperties.RotationOfMass.Euler();
		//EXPECT_TRUE((MassProperties.RotationOfMass.Euler() - FVector(115.8153, -12.4347, 1.9705)).Size() > SMALL_NUMBER);
		//EXPECT_TRUE(MassProperties.InertiaTensor.M[0][0] - 4.99521351 < SMALL_NUMBER);
		//EXPECT_TRUE(MassProperties.InertiaTensor.M[1][1] - 4.07145357 < SMALL_NUMBER);
		//EXPECT_TRUE(MassProperties.InertiaTensor.M[2][2] - 4.26666689 < SMALL_NUMBER);
	}




	TYPED_TEST(AllTraits, GeometryCollection_MassProperties_ScaledSphere)
	{
		// This test has points that are scaled, rotated and translated within mass space. 
		// So the resulting surface is not about the center of mass and needs to be
		// moved for simulation. 

		using Traits = TypeParam;
		using namespace Chaos;
		FVector GlobalTranslation(10); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(45,0,0));
		CreationParameters Params; Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere; Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
		Params.GeomTransform = FTransform(GlobalRotation,GlobalTranslation, FVector(1, 5, 11)); Params.NestedTransforms = { FTransform::Identity, FTransform::Identity,  FTransform::Identity };
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		TArray<FTransform> Transform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->RestCollection->Transform, Collection->RestCollection->Parent, Transform);

		// group ?
		const TManagedArray<bool>& Visible = Collection->RestCollection->Visible;

		// VerticesGroup
		TManagedArray<FVector>& Vertex = Collection->RestCollection->Vertex;

		// GeometryGroup
		const int32 NumGeometries = Collection->RestCollection->NumElements(FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& VertexCount = Collection->RestCollection->VertexCount;
		const TManagedArray<int32>& VertexStart = Collection->RestCollection->VertexStart;
		const TManagedArray<int32>& FaceCount = Collection->RestCollection->FaceCount;
		const TManagedArray<int32>& FaceStart = Collection->RestCollection->FaceStart;
		const TManagedArray<int32>& TransformIndex = Collection->RestCollection->TransformIndex;
		const TManagedArray<FIntVector>& Indices = Collection->RestCollection->Indices;
		const TManagedArray<int32>& BoneMap = Collection->RestCollection->BoneMap;
		int GeometryIndex = 0;

		TUniquePtr<TTriangleMesh<float>> TriMesh(
			CreateTriangleMesh(
				FaceStart[GeometryIndex],
				FaceCount[GeometryIndex],
				Visible,
				Indices,
				false));


		TArray<TMassProperties<float, 3>> MassPropertiesArray;
		MassPropertiesArray.AddUninitialized(NumGeometries);
		TMassProperties<float, 3>& MassProperties = MassPropertiesArray[GeometryIndex];

		TArray<FVector> SomeVec;
		TParticles<float, 3> MassSpaceParticles;
		MassSpaceParticles.AddParticles(Vertex.Num());
		for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
		{
			FVector VertexPoint = Vertex[Idx];
			MassSpaceParticles.X(Idx) = Transform[BoneMap[Idx]].TransformPosition(Vertex[Idx]);
			FVector MassSpacePoint = FVector(MassSpaceParticles.X(Idx)[0], MassSpaceParticles.X(Idx)[1], MassSpaceParticles.X(Idx)[2]);
			SomeVec.Add(MassSpacePoint);
		}

		FBox Bounds(SomeVec);
		CalculateVolumeAndCenterOfMass(MassSpaceParticles, TriMesh->GetElements(), MassProperties.Volume, MassProperties.CenterOfMass);
		
		EXPECT_NEAR(MassProperties.CenterOfMass.X - GlobalTranslation[0], 0.0f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.CenterOfMass.Y - GlobalTranslation[1], 0.0f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.CenterOfMass.Z - GlobalTranslation[2], 0.0f, KINDA_SMALL_NUMBER);

		for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
		{
			MassSpaceParticles.X(Idx) -= MassProperties.CenterOfMass;
		}

		float Density = 0.01f;
		TVector<float, 3> ZeroVec(0);
		CalculateInertiaAndRotationOfMass(MassSpaceParticles, TriMesh->GetSurfaceElements(), Density, ZeroVec, MassProperties.InertiaTensor, MassProperties.RotationOfMass);

		// rotational alignment.
		EXPECT_NEAR(MassProperties.RotationOfMass.Euler()[0], 135.f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.RotationOfMass.Euler()[1], 0, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(MassProperties.RotationOfMass.Euler()[2], 0, KINDA_SMALL_NUMBER);
		// X dominate inertia tensor
		EXPECT_GT(MassProperties.InertiaTensor.M[0][0], MassProperties.InertiaTensor.M[2][2]);
		EXPECT_GT(MassProperties.InertiaTensor.M[0][0], MassProperties.InertiaTensor.M[1][1]);
	}

}