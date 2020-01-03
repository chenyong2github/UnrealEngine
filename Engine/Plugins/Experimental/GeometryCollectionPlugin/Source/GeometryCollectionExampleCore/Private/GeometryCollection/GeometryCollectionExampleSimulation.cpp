// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleSimulation.h"
#include "GeometryCollection/GeometryCollectionExampleUtility.h"
#include "GeometryCollection/GeometryCollectionExampleSimulationObject.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/TransformCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "GeometryCollection/GeometryDynamicCollection.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "Chaos/ErrorReporter.h"
#include "ChaosSolversModule.h"
#include "PBDRigidsSolver.h"

#define SMALL_THRESHOLD 1e-4

// #TODO Lots of duplication in here, anyone making solver or object changes
// has to go and fix up so many callsites here and they're all pretty much
// Identical. The similar code should be pulled out

namespace GeometryCollectionExample
{
	
	template<class T>
	void RigidBodiesFallingUnderGravity()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		//
		//  Rigid Body Setup
		//

		InitCollectionsParameters InitParams = 
		{ 
			FTransform::Identity,	// RestCenter
			FVector(1.0),			// RestScale
			nullptr,				// RestInitFunc
			(int32)EObjectStateTypeEnum::Chaos_Object_Kinematic // DynamicStateDefault
		};

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr; // Allocated and zero'ed
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;				// GeometryCollection::MakeCubeElement(InitParams)
		TSharedPtr<FGeometryDynamicCollection> GTDynamicCollection = nullptr;		// New'ed w/copy of RestCollection Transform, Parent, Children, SimulationType, and StatusFlags attrs.
		InitCollections(PhysicalMaterial, RestCollection, GTDynamicCollection, InitParams);

		//
		// Sim Initialization
		//
		
		// Creates FGeometryCollectionPhysicsProxy with GTDynamicCollection, and 
		// an InitFunc that sets FSimulationParametes's RestCollection and 
		// DynamicCollection pointers.  Calls FGeometryCollectionPhysicsProx::Initialize().
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, GTDynamicCollection);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject); // why?
		Solver->PushPhysicsState(Module->GetDispatcher());

		Solver->AdvanceSolverBy(1 / 24.);

		// Calls BufferPhysicsResults(), FlipBuffer(), and PullFromPhysicsState() on each proxy.
		//FinalizeSolver(*Solver);

		Solver->BufferPhysicsResults();
		Solver->FlipBuffers();
		Solver->UpdateGameThreadStructures();

		// never touched
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		EXPECT_LT(FMath::Abs(RestTransform[0].GetTranslation().Z), SMALL_THRESHOLD);

		// simulated
		TManagedArray<FTransform>& Transform = GTDynamicCollection->Transform;
		EXPECT_EQ(Transform.Num(), 1);
		EXPECT_LT(Transform[0].GetTranslation().Z, 0);
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
	}
	template void RigidBodiesFallingUnderGravity<float>();



	template<class T>
	void RigidBodiesCollidingWithSolverFloor()
	{
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		//  Rigid Body Setup
		//
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), nullptr, (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);
				
		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		};

		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(true);
		Solver->SetIsFloorAnalytic(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);

		FinalizeSolver(*Solver);

		// never touched
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		EXPECT_LT(FMath::Abs(RestTransform[0].GetTranslation().Z), SMALL_THRESHOLD);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		EXPECT_EQ(Transform.Num(), 1);
		EXPECT_LT(FMath::Abs(Transform[0].GetTranslation().Z - 0.5), SMALL_THRESHOLD);
		

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
	}
	template void RigidBodiesCollidingWithSolverFloor<float>();


	template<class T>
	void RigidBodiesSingleSphereCollidingWithSolverFloor()
	{
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		//  Rigid Body Setup
		//
		auto RestInitFunc = [](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->Transform[0].SetTranslation(FVector(0, 0, 10));
		};

		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);
				
		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
		};

		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(true);
		Solver->SetIsFloorAnalytic(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		for (int i = 0; i < 100; i++)
		{
			Solver->AdvanceSolverBy(1 / 240.);
		}
		
		FinalizeSolver(*Solver);

		// never touched
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		EXPECT_LT(FMath::Abs(RestTransform[0].GetTranslation().Z-10.0), KINDA_SMALL_NUMBER);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		TManagedArray<float>& InnerRadius = RestCollection->InnerRadius;
		//UE_LOG(LogTest, Verbose, TEXT("Height : (%3.5f), Inner Radius:%3.5f"), Transform[0].GetTranslation().Z, InnerRadius[0]);

		EXPECT_EQ(Transform.Num(), 1);
		EXPECT_LT(FMath::Abs(Transform[0].GetTranslation().Z - 0.5), 0.1); // @todo - Why is this not 0.5f
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
	}
	template void RigidBodiesSingleSphereCollidingWithSolverFloor<float>();


	template<class T>
	void RigidBodiesSingleSphereIntersectingWithSolverFloor()
	{
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		//  Rigid Body Setup
		//
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), nullptr, (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
		};

		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(true);
		Solver->SetIsFloorAnalytic(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);

		FinalizeSolver(*Solver);

		// never touched
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		EXPECT_LT(FMath::Abs(RestTransform[0].GetTranslation().Z), KINDA_SMALL_NUMBER);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		EXPECT_EQ(Transform.Num(), 1);
		//UE_LOG(LogTest, Verbose, TEXT("Position : (%3.5f,%3.5f,%3.5f)"), Transform[0].GetTranslation().X, Transform[0].GetTranslation().Y, Transform[0].GetTranslation().Z);

		EXPECT_LT(FMath::Abs(Transform[0].GetTranslation().Z - 0.5), KINDA_SMALL_NUMBER);
		

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

	}
	template void RigidBodiesSingleSphereIntersectingWithSolverFloor<float>();


	template<class T>
	void RigidBodiesKinematic()
	{
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		//  Rigid Body Setup
		//
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), nullptr, (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetIsFloorAnalytic(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		for (int i = 0; i < 100; i++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
		}

		FinalizeSolver(*Solver);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		EXPECT_EQ(Transform.Num(), 1);
		//UE_LOG(LogTest, Verbose, TEXT("Position : (%3.5f,%3.5f,%3.5f)"), Transform[0].GetTranslation().X, Transform[0].GetTranslation().Y, Transform[0].GetTranslation().Z);
		EXPECT_EQ(Transform[0].GetTranslation().Z, 0.f);
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

	}
	template void RigidBodiesKinematic<float>();


	template<class T>
	void RigidBodiesSleepingActivation()
	{
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		//  Rigid Body Setup
		//
		auto RestInitFunc = [](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 0)), FVector(100.0)));
			RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 0)), FVector(100.0)));
			RestCollection->Transform[1].SetTranslation(FVector(0.f, 0.f, 5.f));
		};

		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			//InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Cube;
		};

		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);

		TManagedArray<int32>& ObjectType = DynamicCollection->GetAttribute<int32>("DynamicState", FTransformCollection::TransformGroup);
		ObjectType[0] = (int32)EObjectStateTypeEnum::Chaos_Object_Sleeping;
		ObjectType[1] = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetIsFloorAnalytic(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		for (int i = 0; i < 100; i++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			//UE_LOG(LogTest, Verbose, TEXT("Position[0] : (%3.5f,%3.5f,%3.5f)"), Transform[0].GetTranslation().X, Transform[0].GetTranslation().Y, Transform[0].GetTranslation().Z);
			//UE_LOG(LogTest, Verbose, TEXT("Position[1] : (%3.5f,%3.5f,%3.5f)"), Transform[1].GetTranslation().X, Transform[1].GetTranslation().Y, Transform[1].GetTranslation().Z);
		}

		FinalizeSolver(*Solver);

		// @todo(enable): Validated the sleeping object was woken up during the collision. 
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
	}
	template void RigidBodiesSleepingActivation<float>();

	template<class T>
	void RigidBodies_CollisionGroup()
	{
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

		InitCollectionsParameters InitParams = { FTransform(FVector(0, 0, 100.0)), FVector(100.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		//
		// Solver setup
		//
		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		};

		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);
		PhysObject->SetCollisionParticlesPerObjectFraction( 1.0 );

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();

		for (int Frame = 1; Frame < 200; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);
			
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
	}
	template void RigidBodies_CollisionGroup<float>();



	template<class T>
	void RigidBodies_Initialize_ParticleImplicitCollisionGeometry()
	{
		typedef Chaos::TVector<T, 3> Vec;

		typename SimulationObjects<T>::FParameters P;
		P.CollisionGroup = -1;
		P.SizeData.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_LevelSet;
		P.SizeData.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;

		SimulationObjects<T>* Object = new SimulationObjects<T>(P);
		Object->PhysicsProxy->Initialize();

		// check implicit domain
		typedef TUniquePtr<Chaos::FImplicitObject> FImplicitPointer;
		const TManagedArray<FImplicitPointer> & Implicits = Object->RestCollection->template GetAttribute<FImplicitPointer>("Implicits", FTransformCollection::TransformGroup);
		EXPECT_EQ(Implicits.Num(), 1);

		const Chaos::FImplicitObject & Implicit = *Implicits[0];
		for (float x = -1.05; x < 1.0; x += 0.1)
		{
			Vec Normal;
			T phi = Implicit.PhiWithNormal(Vec(x, 0, 0), Normal);
			if (x < -0.5 || 0.5 < x)
			{
				EXPECT_GT(phi, 0);
			}
			else
			{
				EXPECT_LT(phi, 0);
			}

			if (x < -0.25)
			{
				EXPECT_LT((Normal-Vec(-1,0,0)).Size(), KINDA_SMALL_NUMBER);
			}
			else if (x > 0.25)
			{
				EXPECT_LT((Normal - Vec(1, 0, 0)).Size(), KINDA_SMALL_NUMBER);
			}
		}


		// check simplicial elements
		typedef TUniquePtr< FCollisionStructureManager::FSimplicial > FSimplicialPointer;
		const TManagedArray<FSimplicialPointer> & Simplicials = Object->RestCollection->template GetAttribute<FSimplicialPointer>(FGeometryCollectionPhysicsProxy::SimplicialsAttribute, FTransformCollection::TransformGroup);
		EXPECT_EQ(Simplicials.Num(), 1);
		EXPECT_TRUE(Simplicials[0].IsValid());
		EXPECT_EQ(Simplicials[0]->Size(), 8);
		for (int32 Index = 0; Index < (int32)Simplicials[0]->Size(); Index++)
		{
			//const FCollisionStructureManager::FSimplicial & Simplical = Simplicials[0];
			Chaos::TVector<float, 3> Vert = Simplicials[0]->X(Index);
			EXPECT_LT((FMath::Abs(FMath::Abs(Vert.X) + FMath::Abs(Vert.Y) + FMath::Abs(Vert.Z))-1.5), KINDA_SMALL_NUMBER );
		}
		delete Object;
	}
	template void RigidBodies_Initialize_ParticleImplicitCollisionGeometry<float>();

}

