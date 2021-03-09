// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionTestSimulation.h"
#include "GeometryCollection/GeometryCollectionTestUtility.h"
#include "GeometryCollection/GeometryCollectionTestFramework.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/TransformCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "GeometryCollectionProxyData.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "Chaos/ErrorReporter.h"
#include "ChaosSolversModule.h"
#include "PBDRigidsSolver.h"
#include "HeadlessChaosTestUtility.h"

#define SMALL_THRESHOLD 1e-4
#define MEDIUM_THRESHOLD 1e-1

// #TODO Lots of duplication in here, anyone making solver or object changes
// has to go and fix up so many callsites here and they're all pretty much
// Identical. The similar code should be pulled out

namespace GeometryCollectionTest
{
	using namespace ChaosTest;
	
	GTEST_TEST(AllTraits, GeometryCollection_RigidBodies_SingleFallingUnderGravity)
	{
		FGeometryCollectionWrapper* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init()->template As<FGeometryCollectionWrapper>();

		FFramework UnitTest;
		UnitTest.AddSimulationObject(Collection);
		UnitTest.Initialize();
		UnitTest.Advance();

		{ // test results
			EXPECT_LT(FMath::Abs(Collection->RestCollection->Transform[0].GetTranslation().Z), SMALL_THRESHOLD); // rest never touched
			EXPECT_EQ(Collection->DynamicCollection->Transform.Num(), 1); // simulated is falling
			EXPECT_LT(Collection->DynamicCollection->Transform[0].GetTranslation().Z, 0.f);
			EXPECT_NEAR(Collection->DynamicCollection->Transform[0].GetTranslation().Z, -980.f * UnitTest.Dt * UnitTest.Dt, 1e-2);// we seem to be twice gravity
		}
	}

	
	GTEST_TEST(AllTraits, GeometryCollection_RigidBodies_SingleBodyCollidingWithGroundPlane)
	{
		FReal  Scale = 100.0f;
		CreationParameters Params; 
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box; 
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Box;
		FVector BoxScale(Scale); 
		Params.GeomTransform.SetScale3D(BoxScale); // Box dimensions
		Params.GeomTransform.SetLocation(0.9f * Scale * FVector::UpVector);	// Don't start too deep in penetration or the pushout is too aggressive
		FGeometryCollectionWrapper* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init(Params)->template As<FGeometryCollectionWrapper>();
		RigidBodyWrapper* Floor = TNewSimulationObject<GeometryType::RigidFloor>::Init()->template As<RigidBodyWrapper>();

		FFramework UnitTest;
		UnitTest.AddSimulationObject(Collection);
		UnitTest.AddSimulationObject(Floor);
		UnitTest.Initialize();
		for (int i = 0; i < 10; i++)
		{
			UnitTest.Advance();
		}

		{
			EXPECT_LT(FMath::Abs(Collection->RestCollection->Transform[0].GetTranslation().Z), SMALL_THRESHOLD);
			EXPECT_EQ(Collection->DynamicCollection->Transform.Num(), 1);
			EXPECT_LT(FMath::Abs(Collection->DynamicCollection->Transform[0].GetTranslation().Z - 0.1f * Scale), MEDIUM_THRESHOLD * Scale);
		}
	}

	
	GTEST_TEST(AllTraits, GeometryCollection_RigidBodies_SingleSphereCollidingWithSolverFloor)
	{
		FVector Scale(0.5f);
		CreationParameters Params; 
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere; 
		Params.GeomTransform.SetScale3D(Scale); // Sphere radius
		FGeometryCollectionWrapper* Collection =
			TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init(Params)->template As<FGeometryCollectionWrapper>();
		RigidBodyWrapper* Floor = 
			TNewSimulationObject<GeometryType::RigidFloor>::Init()->template As<RigidBodyWrapper>();

		FFramework UnitTest;
		UnitTest.AddSimulationObject(Collection);
		UnitTest.AddSimulationObject(Floor);
		UnitTest.Initialize();
		for (int i = 0; i < 10; i++) UnitTest.Advance();

		{ // test results
			EXPECT_LT(FMath::Abs(Collection->RestCollection->Transform[0].GetTranslation().Z), SMALL_THRESHOLD);
			EXPECT_EQ(Collection->DynamicCollection->Transform.Num(), 1);
			EXPECT_LT(FMath::Abs(Collection->DynamicCollection->Transform[0].GetTranslation().Z) - Scale[0], SMALL_THRESHOLD);
		}
	}


	
	GTEST_TEST(AllTraits, GeometryCollection_RigidBodies_SingleCubeIntersectingWithSolverFloor)
	{
		FVector Scale(100.0f);
		CreationParameters Params; Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;  Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Box;
		Params.GeomTransform.SetScale3D(Scale); // Box size
		Params.GeomTransform.SetLocation(0.9f * Scale * FVector::UpVector);	// Don't start too deep in penetration or the pushout is too aggressive

		FGeometryCollectionWrapper* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init(Params)->template As<FGeometryCollectionWrapper>();
		RigidBodyWrapper* Floor = TNewSimulationObject<GeometryType::RigidFloor>::Init()->template As<RigidBodyWrapper>();

		FFramework UnitTest;
		UnitTest.AddSimulationObject(Collection);
		UnitTest.AddSimulationObject(Floor);
		UnitTest.Initialize();
		for (int i = 0; i < 10; i++)
		{
			UnitTest.Advance();
		}

		{
			EXPECT_LT(FMath::Abs(Collection->RestCollection->Transform[0].GetTranslation().Z), SMALL_THRESHOLD);
			EXPECT_EQ(Collection->DynamicCollection->Transform.Num(), 1);
			EXPECT_LT(FMath::Abs(Collection->DynamicCollection->Transform[0].GetTranslation().Z - 0.1f * Scale[0]), MEDIUM_THRESHOLD * Scale[0]);
		}
	}


	
	GTEST_TEST(AllTraits, GeometryCollection_RigidBodies_SingleKinematicBody)
	{
		CreationParameters Params; Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		FGeometryCollectionWrapper* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init(Params)->template As<FGeometryCollectionWrapper>();

		FFramework UnitTest;
		UnitTest.AddSimulationObject(Collection);
		UnitTest.Initialize();
		for (int i = 0; i < 3; i++)
			UnitTest.Advance();

		{
			TManagedArray<FTransform>& Transform = Collection->DynamicCollection->Transform;
			EXPECT_EQ(Transform.Num(), 1);
			//UE_LOG(LogTest, Verbose, TEXT("Position : (%3.5f,%3.5f,%3.5f)"), Transform[0].GetTranslation().X, Transform[0].GetTranslation().Y, Transform[0].GetTranslation().Z);
			EXPECT_EQ(Transform[0].GetTranslation().Z, 0.f);
			EXPECT_EQ(Collection->DynamicCollection->DynamicState[0], (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic);
		}
	}


	
	GTEST_TEST(AllTraits, GeometryCollection_RigidBodies_SleepingDontMove)
	{
		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Sleeping;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		FReal InitialStartHeight = 5.0;
		Params.RootTransform.SetLocation(FVector(0.f, 0.f, InitialStartHeight));
		FGeometryCollectionWrapper* SleepingCollection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init(Params)->template As<FGeometryCollectionWrapper>();

		FFramework UnitTest;
		UnitTest.AddSimulationObject(SleepingCollection);
		UnitTest.Initialize();

		const auto& Transform0 = SleepingCollection->DynamicCollection->Transform[0];
		for (int i = 0; i < 3; i++)
		{
			UnitTest.Advance();
			//UE_LOG(LogTest, Verbose, TEXT("Position[0] : (%3.5f,%3.5f,%3.5f)"), Transform0.GetTranslation().X, Transform0.GetTranslation().Y, Transform0.GetTranslation().Z);
		}

		{
			// particle doesn't fall due to sleeping state
			EXPECT_EQ(SleepingCollection->DynamicCollection->DynamicState[0], (int32)EObjectStateTypeEnum::Chaos_Object_Sleeping);
			EXPECT_LT(FMath::Abs(SleepingCollection->DynamicCollection->Transform[0].GetTranslation().Z - InitialStartHeight), SMALL_THRESHOLD);
		}

	}


	
	GTEST_TEST(AllTraits, GeometryCollection_RigidBodies_SleepingActivation)
	{
		CreationParameters Params; 
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Box;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;

		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.RootTransform.SetLocation(FVector(0.f, 0.f, 15.f));
		FGeometryCollectionWrapper* MovingCollection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init(Params)->template As<FGeometryCollectionWrapper>();

		FReal InitialStartHeight = 5.0;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Sleeping;
		Params.RootTransform.SetLocation(FVector(0.f, 0.f, InitialStartHeight));
		FGeometryCollectionWrapper* SleepingCollection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init(Params)->template As<FGeometryCollectionWrapper>();

		FFramework UnitTest;
		UnitTest.AddSimulationObject(SleepingCollection);
		UnitTest.AddSimulationObject(MovingCollection);
		UnitTest.Initialize();

		const auto& Transform0 = MovingCollection->DynamicCollection->Transform[0];
		const auto& Transform1 = SleepingCollection->DynamicCollection->Transform[0];
		for (int i = 0; i < 15; i++)
		{
			UnitTest.Advance();

			//UE_LOG(LogTest, Verbose, TEXT("Position[0] : (%3.5f,%3.5f,%3.5f)"), Transform0.GetTranslation().X, Transform0.GetTranslation().Y, Transform0.GetTranslation().Z);
			//UE_LOG(LogTest, Verbose, TEXT("Position[1] : (%3.5f,%3.5f,%3.5f)"), Transform1.GetTranslation().X, Transform1.GetTranslation().Y, Transform1.GetTranslation().Z);
		}

		{
			// Is now dynamic and has moved from initial position
			EXPECT_EQ(SleepingCollection->DynamicCollection->DynamicState[0], (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic);
			EXPECT_LT(Transform0.GetTranslation().Z, InitialStartHeight - 2.0f);
		}

	}

	
	GTEST_TEST(AllTraits, GeometryCollection_RigidBodies_CollisionGroup)
	{
		/*
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		//  Rigid Body Setup
		//
		auto RestInitFunc = [](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 210.0)), FVector(100.0)));
			RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 320.0)), FVector(100.0)));
			RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 430.0)), FVector(100.0)));
		};

		////InitCollectionsParameters InitParams = { FTransform(FVector(0, 0, 100.0)), FVector(100.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic };
		//InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		//
		// Solver setup
		//
		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		};

		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);
		PhysObject->SetCollisionParticlesPerObjectFraction( 1.0 );

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(nullptr, ESolverFlags::Standalone);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		//Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		//PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		Chaos::TPBDRigidParticles<FReal, 3>& Particles = Solver->GetRigidParticles();

		for (int Frame = 1; Frame < 200; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			//FinalizeSolver(*Solver);
			
			if (Frame == 1)
			{
				Particles.CollisionGroup(0)=  0;
				Particles.CollisionGroup(1)=  1;
				Particles.CollisionGroup(2)=  1;
				Particles.CollisionGroup(3)=  3;
				Particles.CollisionGroup(4)= -1;
			}
			if (Frame == 13)
			{
				EXPECT_LT(FMath::Abs(Particles.X(0).Z), SMALL_NUMBER);
				EXPECT_LT(FMath::Abs(Particles.X(1).Z - 50.f), 10.f);
				EXPECT_LT(FMath::Abs(Particles.X(2).Z - 150.f), 10.f);
			}
			if( Frame == 30 )
			{
				EXPECT_LT(FMath::Abs(Particles.X(0).Z), SMALL_NUMBER);
				EXPECT_LT(FMath::Abs(Particles.X(1).Z - 50.f), 10.f);
				EXPECT_LT(FMath::Abs(Particles.X(2).Z - 150.f), 10.f);
				EXPECT_GT(Particles.X(3).Z, 50.f);
				EXPECT_LT(Particles.X(4).Z, -100);
			}
			if (Frame == 31)
			{
				Particles.CollisionGroup(0) = 0;
				Particles.CollisionGroup(1) = -1;
				Particles.CollisionGroup(2) = 1;
				Particles.CollisionGroup(3) = -1;
				Particles.CollisionGroup(4) = -1;
			}
		}

		EXPECT_LT(FMath::Abs(Particles.X(0).Z), SMALL_NUMBER);
		EXPECT_LT(Particles.X(1).Z, -10000);
		EXPECT_GT(Particles.X(2).Z, 50.0);
		EXPECT_LT(Particles.X(3).Z, -10000);
		EXPECT_LT(Particles.X(4).Z, -10000);
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete PhysObject;
		*/
	}



	
	GTEST_TEST(AllTraits, GeometryCollection_TestImplicitCollisionGeometry)
	{
		typedef Chaos::FVec3 Vec;

		CreationParameters Params; 
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_GriddleBox;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_LevelSet;
		Params.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;

		FGeometryCollectionWrapper* Collection =
			TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init(
				Params)->template As<FGeometryCollectionWrapper>();

		const TManagedArray<TUniquePtr<Chaos::FBVHParticles>>& Simplicials =
			Collection->RestCollection->template GetAttribute<TUniquePtr<Chaos::FBVHParticles>>(
				FGeometryDynamicCollection::SimplicialsAttribute, FTransformCollection::TransformGroup);
		EXPECT_EQ(Simplicials.Num(), 1);
		const Chaos::FBVHParticles& Simplicial = *Simplicials[0];

		const TManagedArray<FGeometryDynamicCollection::FSharedImplicit>& Implicits = 
			Collection->RestCollection->template GetAttribute<FGeometryDynamicCollection::FSharedImplicit>(
				FGeometryDynamicCollection::ImplicitsAttribute, FTransformCollection::TransformGroup);
		EXPECT_EQ(Implicits.Num(), 1);
		check(Implicits[0]);
		const Chaos::FImplicitObject& Implicit = *Implicits[0];

		// Ensure all simplicial particles are on the surface of the implicit shape.
		check(Implicit.GetType() == Chaos::ImplicitObjectType::LevelSet);
		const Chaos::FLevelSet* LevelSet = static_cast<const Chaos::FLevelSet*>(&Implicit);
		const FReal DxSize = LevelSet->GetGrid().Dx().Size();

		FReal MinX = TNumericLimits<FReal>::Max();
		FReal MinY = TNumericLimits<FReal>::Max();
		FReal MinZ = TNumericLimits<FReal>::Max();

		FReal MaxX = -TNumericLimits<FReal>::Max();
		FReal MaxY = -TNumericLimits<FReal>::Max();
		FReal MaxZ = -TNumericLimits<FReal>::Max();
		for (uint32 Idx = 0; Idx < Simplicial.Size(); ++Idx)
		{
			const FReal phi = Implicit.SignedDistance(Simplicial.X(Idx));
			EXPECT_LT(FMath::Abs(phi), DxSize);
			//EXPECT_LT(FMath::Abs(phi), 0.01f);

			const auto& Pos = Simplicial.X(Idx);
			MinX = MinX < Pos[0] ? MinX : Pos[0];
			MinY = MinY < Pos[1] ? MinY : Pos[1];
			MinZ = MinZ < Pos[2] ? MinZ : Pos[2];

			MaxX = MaxX > Pos[0] ? MaxX : Pos[0];
			MaxY = MaxY > Pos[1] ? MaxY : Pos[1];
			MaxZ = MaxZ > Pos[2] ? MaxZ : Pos[2];
		}

		// Make sure the geometry occupies a volume.
		check(MinX < MaxX);
		check(MinY < MaxY);
		check(MinZ < MaxZ);

		// Cast a ray through the level set, and make sure it's as we expect.
		for(FReal x = 2*MinX; x < 2*MaxX; x += (MaxX-MinX)/10)
		{
			Vec Normal;
			const FReal phi = Implicit.PhiWithNormal(Vec(x, 0, 0), Normal);
			if (x < MinX || MaxX < x)
			{
				check(phi >= -0.01f);
				EXPECT_GT(phi, -0.01f);
			}
			else
			{
				check(phi <= 0.01f);
				EXPECT_LT(phi, 0.01f);
			}

			if (x < MinX/4)
			{
				EXPECT_LT((Normal-Vec(-1,0,0)).Size(), KINDA_SMALL_NUMBER);
			}
			else if (x > MaxX/4)
			{
				EXPECT_LT((Normal - Vec(1, 0, 0)).Size(), KINDA_SMALL_NUMBER);
			}
		}
	}

}

