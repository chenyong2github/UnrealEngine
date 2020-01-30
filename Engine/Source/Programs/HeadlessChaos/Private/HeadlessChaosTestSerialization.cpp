// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestSerialization.h"

#include "HeadlessChaos.h"
#include "Chaos/ChaosArchive.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/RigidParticles.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/SerializationTestUtility.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/ChaosPerfTest.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Chaos/BoundingVolumeHierarchy.h"
namespace
{
	FString GetSerializedBinaryPath();
}

namespace ChaosTest
{
	using namespace Chaos;

	FString GetSerializedBinaryPath()
	{
		return FPaths::EngineDir() / TEXT("Source/Programs/NotForLicensees/HeadlessChaos/SerializedBinaries");
	}

	template<class T>
	void SimpleObjectsSerialization()
	{

		TArray<TUniquePtr<TSphere<T, 3>>> OriginalSpheres;
		OriginalSpheres.Add(TUniquePtr<TSphere<T, 3>>(new TSphere<T, 3>(TVector<T, 3>(), 1)));
		OriginalSpheres.Add(TUniquePtr<TSphere<T, 3>>(new TSphere<T, 3>(TVector<T, 3>(), 2)));
		OriginalSpheres.Add(TUniquePtr<TSphere<T, 3>>(new TSphere<T, 3>(TVector<T, 3>(), 3)));

		TArray<uint8> Data;
		{
			FMemoryWriter Ar(Data);
			FChaosArchive Writer(Ar);

			Writer << OriginalSpheres;
		}

		{
			FMemoryReader Ar(Data);
			FChaosArchive Reader(Ar);
			TArray<TSerializablePtr<TSphere<T, 3>>> SerializedSpheres;

			Reader << SerializedSpheres;

			EXPECT_TRUE(SerializedSpheres.Num() == OriginalSpheres.Num());

			for (int32 Idx = 0; Idx < SerializedSpheres.Num(); ++Idx)
			{
				EXPECT_TRUE(SerializedSpheres[Idx]->GetRadius() == OriginalSpheres[Idx]->GetRadius());
			}
		}
	}

	template<class T>
	void SharedObjectsSerialization()
	{
		TArray<TSharedPtr<TSphere<T, 3>>> OriginalSpheres;
		TSharedPtr<TSphere<T, 3>> Sphere(new TSphere<T, 3>(TVector<T, 3>(0), 1));
		OriginalSpheres.Add(Sphere);
		OriginalSpheres.Add(Sphere);
		TSerializablePtr<TSphere<T, 3>> SerializableSphere = MakeSerializable(Sphere);

		TArray<uint8> Data;
		{
			FMemoryWriter Ar(Data);
			FChaosArchive Writer(Ar);

			Writer << OriginalSpheres;
			Writer << SerializableSphere;
		}
		
		{
			TArray<TSharedPtr<TSphere<T, 3>>> SerializedSpheres;
			TSerializablePtr<TSphere<T, 3>> SerializedSphere;
			{
				FMemoryReader Ar(Data);
				FChaosArchive Reader(Ar);

				Reader << SerializedSpheres;
				Reader << SerializedSphere;

				EXPECT_TRUE(SerializedSpheres.Num() == OriginalSpheres.Num());
				EXPECT_EQ(SerializedSphere.Get(), SerializedSpheres[0].Get());

				for (int32 Idx = 0; Idx < SerializedSpheres.Num(); ++Idx)
				{
					EXPECT_TRUE(SerializedSpheres[Idx]->GetRadius() == OriginalSpheres[Idx]->GetRadius());
				}

				EXPECT_EQ(SerializedSpheres[0].Get(), SerializedSpheres[1].Get());
				EXPECT_EQ(SerializedSpheres[0].GetSharedReferenceCount(), 3);
			}
			EXPECT_EQ(SerializedSpheres[0].GetSharedReferenceCount(), 2);	//archive is gone so ref count went down
		}
	}

	template<class T>
	void GraphSerialization()
	{
		TArray<TUniquePtr<TSphere<T, 3>>> OriginalSpheres;
		OriginalSpheres.Emplace(new TSphere<T, 3>{ TVector<T,3>(1,2,3), 1 });
		OriginalSpheres.Emplace(new TSphere<T, 3>{ TVector<T,3>(1,2,3), 2 });

		TArray<TUniquePtr<TImplicitObjectTransformed<T, 3>>> OriginalChildren;
		OriginalChildren.Emplace(new TImplicitObjectTransformed<T, 3>(MakeSerializable(OriginalSpheres[0]), TRigidTransform<T,3>::Identity));
		OriginalChildren.Emplace(new TImplicitObjectTransformed<T, 3>(MakeSerializable(OriginalSpheres[1]), TRigidTransform<T,3>::Identity));
		OriginalChildren.Emplace(new TImplicitObjectTransformed<T, 3>(MakeSerializable(OriginalSpheres[0]), TRigidTransform<T,3>::Identity));

		TUniquePtr<TImplicitObjectTransformed<T, 3>> Root(new TImplicitObjectTransformed<T, 3>(MakeSerializable(OriginalChildren[1]), TRigidTransform<T, 3>::Identity));

		TArray<uint8> Data;
		{
			FMemoryWriter Ar(Data);
			FChaosArchive Writer(Ar);

			Writer << OriginalSpheres;
			Writer << OriginalChildren;
			Writer << Root;
		}

		{
			FMemoryReader Ar(Data);
			FChaosArchive Reader(Ar);

			TArray <TUniquePtr<TSphere<T, 3>>> SerializedSpheres;
			TArray<TSerializablePtr<TImplicitObjectTransformed<T, 3>>> SerializedChildren;
			TUniquePtr<TImplicitObjectTransformed<T, 3>> SerializedRoot;

			Reader << SerializedSpheres;
			Reader << SerializedChildren;
			Reader << SerializedRoot;

			EXPECT_EQ(SerializedSpheres.Num(), OriginalSpheres.Num());
			EXPECT_EQ(SerializedChildren.Num(), OriginalChildren.Num());

			EXPECT_EQ(SerializedRoot->GetTransformedObject(), SerializedChildren[1].Get());
			EXPECT_EQ(SerializedChildren[0]->GetTransformedObject(), SerializedSpheres[0].Get());
			EXPECT_EQ(SerializedChildren[1]->GetTransformedObject(), SerializedSpheres[1].Get());
			EXPECT_EQ(SerializedChildren[2]->GetTransformedObject(), SerializedSpheres[0].Get());
		}
	}

	template<class T>
	void ObjectUnionSerialization()
	{
		TArray<TUniquePtr<FImplicitObject>> OriginalSpheres;
		OriginalSpheres.Emplace(new TSphere<T, 3>(TVector<T, 3>(1, 2, 3), 1));
		OriginalSpheres.Emplace(new TSphere<T, 3>(TVector<T, 3>(1, 2, 3), 2));

		TArray<TUniquePtr<FImplicitObject>> OriginalChildren;
		OriginalChildren.Emplace(new TImplicitObjectTransformed<T, 3>(MakeSerializable(OriginalSpheres[0]), TRigidTransform<T, 3>::Identity));
		OriginalChildren.Emplace(new TImplicitObjectTransformed<T, 3>(MakeSerializable(OriginalSpheres[1]), TRigidTransform<T, 3>::Identity));
		OriginalChildren.Emplace(new TImplicitObjectTransformed<T, 3>(MakeSerializable(OriginalSpheres[0]), TRigidTransform<T, 3>::Identity));

		TUniquePtr<FImplicitObjectUnion> Root(new FImplicitObjectUnion(MoveTemp(OriginalChildren)));

		TArray<uint8> Data;
		{
			FMemoryWriter Ar(Data);
			FChaosArchive Writer(Ar);

			Writer << Root;
			Writer << OriginalSpheres;
			Writer << OriginalChildren;
		}

		{
			FMemoryReader Ar(Data);
			FChaosArchive Reader(Ar);

			TArray <TUniquePtr<TSphere<T, 3>>> SerializedSpheres;
			TArray<TSerializablePtr<TImplicitObjectTransformed<T, 3>>> SerializedChildren;
			TUniquePtr<FImplicitObjectUnion> SerializedRoot;

			Reader << SerializedRoot;
			Reader << SerializedSpheres;
			Reader << SerializedChildren;

			EXPECT_EQ(SerializedSpheres.Num(), OriginalSpheres.Num());
			EXPECT_EQ(SerializedChildren.Num(), OriginalChildren.Num());
			EXPECT_EQ(SerializedChildren.Num(), 0);	//We did a move and then serialized, should be empty

			const TArray<TUniquePtr<FImplicitObject>>& UnionObjs = SerializedRoot->GetObjects();
			TImplicitObjectTransformed<T, 3>* FirstChild = static_cast<TImplicitObjectTransformed<T, 3>*>(UnionObjs[0].Get());
			TImplicitObjectTransformed<T, 3>* SecondChild = static_cast<TImplicitObjectTransformed<T, 3>*>(UnionObjs[1].Get());
			TImplicitObjectTransformed<T, 3>* ThirdChild = static_cast<TImplicitObjectTransformed<T, 3>*>(UnionObjs[2].Get());

			EXPECT_EQ(FirstChild->GetTransformedObject(), SerializedSpheres[0].Get());
			EXPECT_EQ(SecondChild->GetTransformedObject(), SerializedSpheres[1].Get());
			EXPECT_EQ(ThirdChild->GetTransformedObject(), SerializedSpheres[0].Get());
			EXPECT_TRUE(FirstChild != ThirdChild);	//First and third point to same sphere, but still unique children
		}
	}

	template<class T>
	void ParticleSerialization()
	{
		TArray<TUniquePtr<TSphere<T, 3>>> OriginalSpheres;
		OriginalSpheres.Emplace(new TSphere<T, 3>(TVector<T, 3>(1, 2, 3), 1));
		OriginalSpheres.Emplace(new TSphere<T, 3>(TVector<T, 3>(1, 2, 3), 2));

		{
			TGeometryParticles<T, 3> OriginalParticles;
			OriginalParticles.AddParticles(2);
			OriginalParticles.SetGeometry(0, MakeSerializable(OriginalSpheres[0]));
			OriginalParticles.SetGeometry(1, MakeSerializable(OriginalSpheres[1]));

			TArray<uint8> Data;
			{
				FMemoryWriter Ar(Data);
				FChaosArchive Writer(Ar);

				Writer << OriginalParticles;
				Writer << OriginalSpheres;
			}

			{
				FMemoryReader Ar(Data);
				FChaosArchive Reader(Ar);

				TArray <TUniquePtr<TSphere<T, 3>>> SerializedSpheres;
				TGeometryParticles<T, 3> SerializedParticles;

				Reader << SerializedParticles;
				Reader << SerializedSpheres;

				EXPECT_EQ(SerializedSpheres.Num(), OriginalSpheres.Num());
				EXPECT_EQ(SerializedParticles.Size(), OriginalParticles.Size());

				EXPECT_EQ(SerializedParticles.Geometry(0).Get(), SerializedSpheres[0].Get());
				EXPECT_EQ(SerializedParticles.Geometry(1).Get(), SerializedSpheres[1].Get());
			}
		}

		//ptr
		{
			auto OriginalParticles = MakeUnique<TGeometryParticles<T, 3>>();
			OriginalParticles->AddParticles(2);
			OriginalParticles->SetGeometry(0, MakeSerializable(OriginalSpheres[0]));
			OriginalParticles->SetGeometry(1, MakeSerializable(OriginalSpheres[1]));

			TArray<uint8> Data;
			{
				FMemoryWriter Ar(Data);
				FChaosArchive Writer(Ar);

				Writer << OriginalParticles;
				Writer << OriginalSpheres;
			}

			{
				FMemoryReader Ar(Data);
				FChaosArchive Reader(Ar);

				TArray <TUniquePtr<TSphere<T, 3>>> SerializedSpheres;
				TUniquePtr<TGeometryParticles<T, 3>> SerializedParticles;

				Reader << SerializedParticles;
				Reader << SerializedSpheres;

				EXPECT_EQ(SerializedSpheres.Num(), OriginalSpheres.Num());
				EXPECT_EQ(SerializedParticles->Size(), OriginalParticles->Size());

				EXPECT_EQ(SerializedParticles->Geometry(0).Get(), SerializedSpheres[0].Get());
				EXPECT_EQ(SerializedParticles->Geometry(1).Get(), SerializedSpheres[1].Get());
			}
		}
	}

	template<class T>
	void BVHSerialization()
	{
		TArray<uint8> Data;
		{
			TArray<TUniquePtr<TSphere<T, 3>>> OriginalSpheres;
			OriginalSpheres.Emplace(new TSphere<T, 3>(TVector<T, 3>(0, 0, 0), 1));
			OriginalSpheres.Emplace(new TSphere<T, 3>(TVector<T, 3>(0, 0, 0), 2));

			TGeometryParticles<T, 3> OriginalParticles;
			OriginalParticles.AddParticles(2);
			OriginalParticles.SetGeometry(0, MakeSerializable(OriginalSpheres[0]));
			OriginalParticles.SetGeometry(1, MakeSerializable(OriginalSpheres[1]));
			OriginalParticles.X(0) = TVector<T, 3>(100, 1, 2);
			OriginalParticles.X(1) = TVector<T, 3>(0, 1, 2);
			OriginalParticles.R(0) = TRotation<T, 3>::Identity;
			OriginalParticles.R(1) = TRotation<T, 3>::Identity;

			TBoundingVolumeHierarchy<TGeometryParticles<T, 3>, TArray<int32>, T, 3> OriginalBVH(OriginalParticles);

			FMemoryWriter Ar(Data);
			FChaosArchive Writer(Ar);

			Writer << OriginalBVH;
			Writer << OriginalSpheres;
			Writer << OriginalParticles;
		}

		{
			TArray <TUniquePtr<TSphere<T, 3>>> SerializedSpheres;
			TGeometryParticles<T, 3> SerializedParticles;
			TBoundingVolumeHierarchy<TGeometryParticles<T, 3>, TArray<int32>, T, 3> SerializedBVH(SerializedParticles);
			FMemoryReader Ar(Data);
			FChaosArchive Reader(Ar);

			Reader << SerializedBVH;
			Reader << SerializedSpheres;
			Reader << SerializedParticles;

			const TAABB<T, 3> QueryBox{ {-1,0,0}, {1,10,20} };
			const TArray<int32>& PotentialIntersections = SerializedBVH.FindAllIntersections(QueryBox);
			TArray<int32> FinalIntersections;
			for (int32 Potential : PotentialIntersections)
			{
				TRigidTransform<T, 3> TM(SerializedParticles.X(Potential), SerializedParticles.R(Potential));
				const TAABB<T, 3> Bounds = SerializedParticles.Geometry(Potential)->BoundingBox().TransformedAABB(TM);
				if (Bounds.Intersects(QueryBox))
				{
					FinalIntersections.Add(Potential);
				}
			}

			EXPECT_EQ(FinalIntersections.Num(), 1);
			EXPECT_EQ(FinalIntersections[0], 1);
		}
	}

	template<class T>
	void RigidParticlesSerialization()
	{
		TArray<TVector<T, 3>> F;
		F.Emplace(TVector<T, 3>(1, 2, 3));
		F.Emplace(TVector<T, 3>(3, 2, 1));

		TArray<TVector<T, 3>> X;
		X.Emplace(TVector<T, 3>(0, 2, 1));
		X.Emplace(TVector<T, 3>(100, 15, 0));

		TRigidParticles<T, 3> Particles;
		Particles.AddParticles(2);
		Particles.F(0) = F[0];
		Particles.F(1) = F[1];
		Particles.X(0) = X[0];
		Particles.X(1) = X[1];


		TCHAR const * BinaryFolderName = TEXT("RigidParticles");
		bool bSaveBinaryToDisk = false; // Flip to true and run to save current binary to disk for future tests.
		TArray<TRigidParticles<T, 3>> ObjectsToTest;
		bool bResult = SaveLoadUtility<T,TRigidParticles<T, 3>>(Particles, *GetSerializedBinaryPath(), BinaryFolderName, bSaveBinaryToDisk, ObjectsToTest);
		EXPECT_TRUE(bResult);

		for (TRigidParticles<T, 3> const &TestParticles : ObjectsToTest)
		{
			EXPECT_EQ(TestParticles.Size(), Particles.Size());
			EXPECT_EQ(TestParticles.F(0), Particles.F(0));
			EXPECT_EQ(TestParticles.F(1), Particles.F(1));
			EXPECT_EQ(TestParticles.X(0), Particles.X(0));
			EXPECT_EQ(TestParticles.X(1), Particles.X(1));
		}
	}

	template<class T>
	void BVHParticlesSerialization()
	{
		TArray<uint8> Data;
		TArray<TUniquePtr<TSphere<T, 3>>> Spheres;
		Spheres.Emplace(new TSphere<T, 3>(TVector<T, 3>(0, 0, 0), 1));
		Spheres.Emplace(new TSphere<T, 3>(TVector<T, 3>(0, 0, 0), 1));
		Spheres.Emplace(new TSphere<T, 3>(TVector<T, 3>(0, 0, 0), 1));

		TGeometryParticles<T, 3> Particles;
		Particles.AddParticles(3);
		Particles.SetGeometry(0, MakeSerializable(Spheres[0]));
		Particles.SetGeometry(1, MakeSerializable(Spheres[1]));
		Particles.SetGeometry(2, MakeSerializable(Spheres[2]));
		Particles.X(0) = TVector<T, 3>(15, 1, 2);
		Particles.X(1) = TVector<T, 3>(0, 2, 2);
		Particles.X(2) = TVector<T, 3>(0, 2, 2);
		Particles.R(0) = TRotation<T, 3>::Identity;
		Particles.R(1) = TRotation<T, 3>::Identity;
		Particles.R(2) = TRotation<T, 3>::Identity;

		TBVHParticles<T, 3> BVHParticles(MoveTemp(Particles));

		TCHAR const *BinaryFolderName = TEXT("BVHParticles");
		bool bSaveBinaryToDisk = false; // Flip to true and run to save current binary to disk for future tests.
		TArray<TBVHParticles<T, 3>> ObjectsToTest;
		bool bResult = SaveLoadUtility<T, TBVHParticles<T, 3>>(BVHParticles, *GetSerializedBinaryPath(), BinaryFolderName, bSaveBinaryToDisk, ObjectsToTest);
		EXPECT_TRUE(bResult);

		for (TBVHParticles<T, 3> const &TestBVHP: ObjectsToTest)
		{
			const TAABB<T, 3> Box{ {-1,-1,-1}, {1,3,3} };
			TArray<int32> PotentialIntersections = BVHParticles.FindAllIntersections(Box);

			EXPECT_EQ(TestBVHP.Size(), BVHParticles.Size());
			EXPECT_EQ(PotentialIntersections.Num(), 2);
			EXPECT_EQ(PotentialIntersections[0], 1);
			EXPECT_EQ(PotentialIntersections[1], 2);
		}
	}

	void EvolutionPerfHelper(const FString& FilePath)
	{
		CHAOS_PERF_TEST(EvolutionPerf, EChaosPerfUnits::Us);

		for (int i = 0; i < 1000; ++i)
		{
			TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*FilePath));
			if (File)
			{
				Chaos::FChaosArchive ChaosAr(*File);
				TPBDRigidsSOAs<float, 3> Particles;
				TPBDRigidsEvolutionGBF<float, 3> Evolution(Particles);

				Evolution.Serialize(ChaosAr);
				Evolution.AdvanceOneTimeStep(1 / 60.f);
				Evolution.EndFrame(1 / 60.0f);
			}
		}
	}

	void EvolutionPerfHarness()
	{
		//Load evolutions and step them over and over (with rewind) to measure perf of different components in the system
		//EvolutionPerfHelper(FPaths::EngineDir() / TEXT("Source/Programs/NotForLicensees/HeadlessPhysicsSQ/Captures/ChaosEvolution_76.bin"));
	}
	

	template void SimpleObjectsSerialization<float>();
	template void SharedObjectsSerialization<float>();
	template void GraphSerialization<float>();
	template void ObjectUnionSerialization<float>();
	template void ParticleSerialization<float>();
	template void BVHSerialization<float>();
	template void RigidParticlesSerialization<float>();
	template void BVHParticlesSerialization<float>();
}