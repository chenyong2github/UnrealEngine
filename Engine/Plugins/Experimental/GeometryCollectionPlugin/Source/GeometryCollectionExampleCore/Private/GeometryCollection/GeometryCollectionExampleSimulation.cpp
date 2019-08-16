// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	bool RigidBodiesFallingUnderGravity(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());
		
		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);

		FinalizeSolver(*Solver);

		// never touched
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		R.ExpectTrue(FMath::Abs(RestTransform[0].GetTranslation().Z) < SMALL_THRESHOLD);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		R.ExpectTrue(Transform.Num() == 1);
		R.ExpectTrue(Transform[0].GetTranslation().Z < 0);
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

#endif


		return !R.HasError();
	}
	template bool RigidBodiesFallingUnderGravity<float>(ExampleResponse&& R);



	template<class T>
	bool RigidBodiesCollidingWithSolverFloor(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());
				
		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

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
		R.ExpectTrue(FMath::Abs(RestTransform[0].GetTranslation().Z) < SMALL_THRESHOLD);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		R.ExpectTrue(Transform.Num() == 1);
		R.ExpectTrue(FMath::Abs(Transform[0].GetTranslation().Z - 0.5) < SMALL_THRESHOLD);
		

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

#endif


		return !R.HasError();
	}
	template bool RigidBodiesCollidingWithSolverFloor<float>(ExampleResponse&& R);


	template<class T>
	bool RigidBodiesSingleSphereCollidingWithSolverFloor(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		(RestCollection->Transform)[0].SetTranslation(FVector(0, 0, 10));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());
				
		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

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
		R.ExpectTrue(FMath::Abs(RestTransform[0].GetTranslation().Z-10.0) < KINDA_SMALL_NUMBER);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		TManagedArray<float>& InnerRadius = RestCollection->InnerRadius;
		//UE_LOG(LogTest, Verbose, TEXT("Height : (%3.5f), Inner Radius:%3.5f"), Transform[0].GetTranslation().Z, InnerRadius[0]);

		R.ExpectTrue(Transform.Num() == 1);
		R.ExpectTrue(FMath::Abs(Transform[0].GetTranslation().Z - 0.5) < 0.1); // @todo - Why is this not 0.5f
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

#endif


		return !R.HasError();
	}
	template bool RigidBodiesSingleSphereCollidingWithSolverFloor<float>(ExampleResponse&& R);


	template<class T>
	bool RigidBodiesSingleSphereIntersectingWithSolverFloor(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

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
		R.ExpectTrue(FMath::Abs(RestTransform[0].GetTranslation().Z) < KINDA_SMALL_NUMBER);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		R.ExpectTrue(Transform.Num() == 1);
		//UE_LOG(LogTest, Verbose, TEXT("Position : (%3.5f,%3.5f,%3.5f)"), Transform[0].GetTranslation().X, Transform[0].GetTranslation().Y, Transform[0].GetTranslation().Z);

		R.ExpectTrue(FMath::Abs(Transform[0].GetTranslation().Z - 0.5) < KINDA_SMALL_NUMBER);
		

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

#endif

		return !R.HasError();
	}
	template bool RigidBodiesSingleSphereIntersectingWithSolverFloor<float>(ExampleResponse&& R);


	template<class T>
	bool RigidBodiesKinematic(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic);

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

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
		R.ExpectTrue(Transform.Num() == 1);
		//UE_LOG(LogTest, Verbose, TEXT("Position : (%3.5f,%3.5f,%3.5f)"), Transform[0].GetTranslation().X, Transform[0].GetTranslation().Y, Transform[0].GetTranslation().Z);
		R.ExpectTrue(Transform[0].GetTranslation().Z == 0.f);
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

#endif

		return !R.HasError();
	}
	template bool RigidBodiesKinematic<float>(ExampleResponse&& R);


	template<class T>
	bool RigidBodiesSleepingActivation(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));

		RestCollection->AppendGeometry(*RestCollection);
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		RestTransform[1].SetTranslation(FVector(0.f, 0.f, 5.f));

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic);

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			//InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Cube;
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		//
		//
		//
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

#endif
		return !R.HasError();
	}
	template bool RigidBodiesSleepingActivation<float>(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_CollisionGroup(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		//
		// Generate Geometry
		//
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 100.0)), FVector(100.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 210.0)), FVector(100.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 320.0)), FVector(100.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 430.0)), FVector(100.0)));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

		//
		// Solver setup
		//
		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();
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
				R.ExpectTrue(FMath::Abs(Particles.X(0).Z) < SMALL_NUMBER);
				R.ExpectTrue(FMath::Abs(Particles.X(1).Z - 50.f) < 10.f);
				R.ExpectTrue(FMath::Abs(Particles.X(2).Z - 150.f) < 10.f);
			}
			if( Frame == 30 )
			{
				R.ExpectTrue(FMath::Abs(Particles.X(0).Z) < SMALL_NUMBER);
				R.ExpectTrue(FMath::Abs(Particles.X(1).Z - 50.f) < 10.f);
				R.ExpectTrue(FMath::Abs(Particles.X(2).Z - 150.f) < 10.f);
				R.ExpectTrue(Particles.X(3).Z > 50.f);
				R.ExpectTrue(Particles.X(4).Z < -100);
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

		R.ExpectTrue(FMath::Abs(Particles.X(0).Z) < SMALL_NUMBER);
		R.ExpectTrue(Particles.X(1).Z < -10000);
		R.ExpectTrue(Particles.X(2).Z > 50.0);
		R.ExpectTrue(Particles.X(3).Z < -10000);
		R.ExpectTrue(Particles.X(4).Z < -10000);
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete PhysObject;
#endif

		return !R.HasError();
	}
	template bool RigidBodies_CollisionGroup<float>(ExampleResponse&& R);



	template<class T>
	bool RigidBodies_Initialize_ParticleImplicitCollisionGeometry(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		typedef Chaos::TVector<T, 3> Vec;

		typename SimulationObjects<T>::FParameters P;
		P.CollisionGroup = -1;
		P.SizeData.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_LevelSet;
		P.SizeData.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;

		SimulationObjects<T>* Object = new SimulationObjects<T>(P);
		Object->PhysicsProxy->Initialize();

		// check implicit domain
		typedef TUniquePtr<Chaos::TImplicitObject<float, 3>> FImplicitPointer;
		const TManagedArray<FImplicitPointer> & Implicits = Object->RestCollection->template GetAttribute<FImplicitPointer>("Implicits", FTransformCollection::TransformGroup);
		R.ExpectTrue(Implicits.Num() == 1);

		const Chaos::TImplicitObject<float, 3> & Implicit = *Implicits[0];
		for (float x = -1.05; x < 1.0; x += 0.1)
		{
			Vec Normal;
			T phi = Implicit.PhiWithNormal(Vec(x, 0, 0), Normal);
			if (x < -0.5 || 0.5 < x)
			{
				R.ExpectTrue(phi > 0);
			}
			else
			{
				R.ExpectTrue(phi < 0);
			}

			if (x < -0.25)
			{
				R.ExpectTrue((Normal-Vec(-1,0,0)).Size()<KINDA_SMALL_NUMBER);
			}
			else if (x > 0.25)
			{
				R.ExpectTrue((Normal - Vec(1, 0, 0)).Size() < KINDA_SMALL_NUMBER);
			}
		}


		// check simplicial elements
		typedef TUniquePtr< FCollisionStructureManager::FSimplicial > FSimplicialPointer;
		const TManagedArray<FSimplicialPointer> & Simplicials = Object->RestCollection->template GetAttribute<FSimplicialPointer>(FGeometryCollectionPhysicsProxy::SimplicialsAttribute, FTransformCollection::TransformGroup);
		R.ExpectTrue(Simplicials.Num() == 1);
		R.ExpectTrue(Simplicials[0].IsValid());
		R.ExpectTrue(Simplicials[0]->Size() == 8);
		for (int32 Index = 0; Index < (int32)Simplicials[0]->Size(); Index++)
		{
			//const FCollisionStructureManager::FSimplicial & Simplical = Simplicials[0];
			Chaos::TVector<float, 3> Vert = Simplicials[0]->X(Index);
			R.ExpectTrue((FMath::Abs(FMath::Abs(Vert.X) + FMath::Abs(Vert.Y) + FMath::Abs(Vert.Z))-1.5)<KINDA_SMALL_NUMBER );
		}
		delete Object;
#endif
		return !R.HasError();
	}
	template bool RigidBodies_Initialize_ParticleImplicitCollisionGeometry<float>(ExampleResponse&& R);

}

