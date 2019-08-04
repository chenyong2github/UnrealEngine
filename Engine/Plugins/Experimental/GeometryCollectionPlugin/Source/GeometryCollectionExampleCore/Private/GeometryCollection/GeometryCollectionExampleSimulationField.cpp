// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleSimulationField.h"
#include "GeometryCollection/GeometryCollectionExampleUtility.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/TransformCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "Field/FieldSystem.h"
#include "Field/FieldSystemNodes.h"
#include "GeometryCollection/GeometryDynamicCollection.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/PBDRigidClustering.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"

#define SMALL_THRESHOLD 1e-4

// #TODO Lots of duplication in here, anyone making solver or object changes
// has to go and fix up so many callsites here and they're all pretty much
// Identical. The similar code should be pulled out

namespace GeometryCollectionExample
{


	template<class T>
	bool RigidBodies_Field_KinematicActivation(ExampleResponse&& R)
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
		RestCollection->Transform[0].SetTranslation(FVector(0, 0, 1));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic);

		FRadialIntMask * RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, 0.0);
		RadialMask->Radius = 100.0;
		RadialMask->InteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->ExteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_IFF_NOT_Interior;

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
		R.ExpectTrue(Transform[0].GetTranslation().Z == 1.f);
		FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState);
		PhysObject->BufferCommand(Solver, { TargetName, RadialMask });

		for (int i = 0; i < 100; i++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
		}

		FinalizeSolver(*Solver);

		R.ExpectTrue(Transform[0].GetTranslation().Z <= 0.f);	
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete PhysObject;

#endif


		return !R.HasError();
	}
	template bool RigidBodies_Field_KinematicActivation<float>(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_InitialLinearVelocity(ExampleResponse&& R)
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
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(),(int)EObjectStateTypeEnum::Chaos_Object_Kinematic);

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			InParams.InitialVelocityType = EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined;
			InParams.InitialLinearVelocity = FVector(0.f, 100.f, 0.f);
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};


		//
		// Field setup
		//
		FRadialIntMask * RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, 0.0);
		RadialMask->Radius = 0;
		RadialMask->InteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->ExteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_Always;

		//
		// Solver setup
		//
		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetIsFloorAnalytic(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;

		float PreviousY = 0.f;
		R.ExpectTrue(Transform[0].GetTranslation().X == 0);
		R.ExpectTrue(Transform[0].GetTranslation().Y == 0);

		for (int Frame = 0; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);

			if (Frame == 1)
			{
				FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState);
				PhysObject->BufferCommand(Solver, { TargetName, RadialMask });
			}

			FinalizeSolver(*Solver);
			if (Frame >= 2)
			{
				R.ExpectTrue(Transform[0].GetTranslation().X == 0);
				R.ExpectTrue(Transform[0].GetTranslation().Y > PreviousY);
			}
			else
			{
				R.ExpectTrue(Transform[0].GetTranslation().X == 0);
				R.ExpectTrue(Transform[0].GetTranslation().Y == 0);
				R.ExpectTrue(Transform[0].GetTranslation().Z == 0);
			}
			PreviousY = Transform[0].GetTranslation().Y;
		}
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete PhysObject;

#endif


		return !R.HasError();
	}
	template bool RigidBodies_Field_InitialLinearVelocity<float>(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_StayDynamic(ExampleResponse&& R)
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
		//  Rigid Body Setup
		//
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;

		RestTransform[0].SetTranslation(FVector(0.f, 0.f, 5.f));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (int32)EObjectStateTypeEnum::Chaos_Object_Static);

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

		//
		// Field setup
		//
		FRadialIntMask * RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, 5.0);
		RadialMask->Radius = 5.0;
		RadialMask->InteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->ExteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_IFF_NOT_Interior;


		//
		// Solver setup
		//
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		float PreviousHeight = 5.f;
		for (int Frame = 0; Frame < 10; Frame++)
		{
			//UE_LOG(LogTest, Verbose, TEXT("Frame[%d]"), Frame);

			if (Frame == 5)
			{
				FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState);
				FieldObject->BufferCommand(Solver, { TargetName, RadialMask });
			}

			Solver->AdvanceSolverBy(1 / 24.);

			FinalizeSolver(*Solver);

			if (Frame < 5)
			{
				R.ExpectTrue(FMath::Abs(Transform[0].GetTranslation().Z - 5.f) < SMALL_THRESHOLD);
			}
			else
			{
				R.ExpectTrue(Transform[0].GetTranslation().Z < PreviousHeight);
			}
			PreviousHeight = Transform[0].GetTranslation().Z;

			//UE_LOG(LogTest, Verbose, TEXT("Position[0] : (%3.5f,%3.5f,%3.5f)"), Transform[0].GetTranslation().X, Transform[0].GetTranslation().Y, Transform[0].GetTranslation().Z);
			//for (int32 rdx = 0; rdx < (int32)Particles.Size(); rdx++)
			//{
			//	UE_LOG(GCTCL_Log, Verbose, TEXT("... ... ...Disabled[%d] : %d"), rdx, Particles.Disabled(rdx));
			//}
		}
		
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FieldObject;

#endif
		return !R.HasError();
	}
	template bool RigidBodies_Field_StayDynamic<float>(ExampleResponse&& R);


	template<class T>
	bool RigidBodies_Field_LinearForce(ExampleResponse&& R)
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
		//  Rigid Body Setup
		//
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		RestTransform[0].SetTranslation(FVector(0.f, 0.f, 5.f));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic);

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

		//
		// Field setup
		//
		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(0.0, 1.0, 0.0);
		UniformVector->Magnitude = 1000.0;

		//
		// Solver setup
		//
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		float PreviousY = 0.f;
		for (int Frame = 0; Frame < 10; Frame++)
		{

			if (Frame >= 5)
			{
				FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_LinearForce);
				FieldObject->BufferCommand(Solver, { TargetName, UniformVector->NewCopy() });
			}

			Solver->AdvanceSolverBy(1 / 24.);

			FinalizeSolver(*Solver);

			if (Frame < 5)
			{
				R.ExpectTrue(FMath::Abs(Transform[0].GetTranslation().Y) < SMALL_THRESHOLD);
			}
			else
			{
				R.ExpectTrue(Transform[0].GetTranslation().Y > PreviousY);
			}

			PreviousY = Transform[0].GetTranslation().Y;

		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete UniformVector;
		delete PhysObject;
		delete FieldObject;
#endif

		return !R.HasError();		
	}
	template bool RigidBodies_Field_LinearForce<float>(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_Torque(ExampleResponse&& R)
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
		//  Rigid Body Setup
		//
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(10.0));
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		RestTransform[0].SetTranslation(FVector(0.f, 0.f, 5.f));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic);

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

		//
		// Field setup
		//
		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(0.0, 1.0, 0.0);
		UniformVector->Magnitude = 100.0;

		//
		// Solver setup
		//
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		//TManagedArray<FVector>& AngularVelocity = DynamicCollection->;
		float PreviousY = 0.f;
		for (int Frame = 0; Frame < 10; Frame++)
		{

			if (Frame >= 5)
			{
				FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_AngularTorque);
				FieldObject->BufferCommand(Solver, { TargetName, UniformVector->NewCopy() });
			}

			Solver->AdvanceSolverBy(1 / 24.);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			const Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();

			FinalizeSolver(*Solver);

			if (Frame < 5)
			{
				R.ExpectTrue(FMath::Abs(Transform[0].GetRotation().Euler().Y) < SMALL_THRESHOLD);
			}
			else
			{
				R.ExpectTrue(FMath::Abs(Transform[0].GetRotation().Euler().Y) != SMALL_THRESHOLD);
				R.ExpectTrue(Particles.W(0).Y > PreviousY);
			}

			PreviousY = Particles.W(0).Y;
#endif

		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete UniformVector;
		delete PhysObject;
		delete FieldObject;
#endif

		return !R.HasError();
	}
	template bool RigidBodies_Field_Torque<float>(ExampleResponse&& R);



	template<class T>
	bool RigidBodies_Field_Kill(ExampleResponse&& R)
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
		//  Rigid Body Setup
		//
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;

		RestTransform[0].SetTranslation(FVector(0.f, 0.f, 20.f));
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

		//
		// Field setup
		//
		FPlaneFalloff * FalloffField = new FPlaneFalloff();
		FalloffField->Magnitude = 1.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Normal = FVector(0.0, 0.0, 1.0);
		FalloffField->Falloff = EFieldFalloffType::Field_Falloff_Inverse;

		//
		// Solver setup
		//
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		for (int Frame = 0; Frame < 10; Frame++)
		{
			//UE_LOG(LogTest, Verbose, TEXT("Frame[%d]"), Frame);

			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_Kill);
			FieldObject->BufferCommand(Solver, { TargetName, FalloffField->NewCopy() });

			Solver->AdvanceSolverBy(1 / 24.);

			FinalizeSolver(*Solver);

			R.ExpectTrue(Transform[0].GetTranslation().Z < 20.f);
			R.ExpectTrue(Transform[0].GetTranslation().Z > -10.);
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FieldObject;
		delete FalloffField;

#endif
		return !R.HasError();
	}
	template bool RigidBodies_Field_Kill<float>(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_LinearVelocity(ExampleResponse&& R)
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
		//  Rigid Body Setup
		//
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;

		RestTransform[0].SetTranslation(FVector(0.f, 0.f, 20.f));
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

		//
		// Field setup
		//
		FUniformVector * VectorField = new FUniformVector();
		VectorField->Magnitude = 100.0;
		VectorField->Direction = FVector(1.0, 0.0, 0.0);

		//
		// Solver setup
		//
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity);
		FieldObject->BufferCommand(Solver, { TargetName, VectorField->NewCopy() });
		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		float PreviousX = 0.f;
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		for (int Frame = 1; Frame < 10; Frame++)
		{
			FieldObject->BufferCommand(Solver, { GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity), VectorField->NewCopy() });

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

			R.ExpectTrue(Transform[0].GetTranslation().X > PreviousX);
			PreviousX = Transform[0].GetTranslation().X;
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FieldObject;
		delete VectorField;

#endif
		return !R.HasError();
	}
	template bool RigidBodies_Field_LinearVelocity<float>(ExampleResponse&& R);


	/**
	 * Create a stack of boxes on the ground and verify that we we change their collision 
	 * group, they drop through the ground.
	 */
	template<class T>
	bool RigidBodies_Field_CollisionGroup(ExampleResponse&& R)
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
		// Generate Geometry - a stack of boxes.
		// The bottom box is on the ground, and the others are dropped into it.
		//
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 100)), FVector(200));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 400)), FVector(100)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 600)), FVector(100)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 800)), FVector(100)));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

		//
		// Field setup
		//
		FRadialIntMask * RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, 0.0);
		RadialMask->Radius = 0;
		RadialMask->InteriorValue = -1;
		RadialMask->ExteriorValue = -1;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_Always;

		//
		// Solver setup
		//

		//
		// Solver setup
		//
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
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();


#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		for (int Frame = 0; Frame < 60; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

			Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
			if (Frame == 30)
			{
				// The boxes should have landed on each other and settled by now
				R.ExpectTrue(FMath::Abs(Particles.X(0).Z) < SMALL_NUMBER);
				R.ExpectTrue(FMath::IsNearlyEqual(Particles.X(1).Z, (T)100, (T)2));
				R.ExpectTrue(FMath::IsNearlyEqual(Particles.X(2).Z, (T)250, (T)2));
				R.ExpectTrue(FMath::IsNearlyEqual(Particles.X(3).Z, (T)350, (T)2));
				R.ExpectTrue(FMath::IsNearlyEqual(Particles.X(4).Z, (T)450, (T)2));
			}
			if (Frame == 31)
			{
				FName TargetName = GetGeometryCollectionPhysicsTypeName(EGeometryCollectionPhysicsTypeEnum::Chaos_CollisionGroup);
				PhysObject->BufferCommand(Solver, { TargetName, RadialMask });
			}
		}
		// The boxes should have fallen below the ground level
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
		R.ExpectTrue(FMath::Abs(Particles.X(0).Z) < SMALL_NUMBER);
		R.ExpectTrue(Particles.X(1).Z < 0);
		R.ExpectTrue(Particles.X(2).Z < 0);
		R.ExpectTrue(Particles.X(3).Z < 0);
		R.ExpectTrue(Particles.X(4).Z < 0);
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
#endif

		return !R.HasError();
	}
	template bool RigidBodies_Field_CollisionGroup<float>(ExampleResponse&& R);


	template<class T>
	bool RigidBodies_Field_ClusterBreak_StrainModel_Test1(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_TwoByTwo_ThreeTransform(FVector(0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.ClusterConnectionMethod = Chaos::FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation;
			InParams.MaxClusterLevel = 1000;
			InParams.ClusterGroupIndex = 0;
			InParams.DamageThreshold = { 1 };
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};


		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.5;
		FalloffField->Radius = 100.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();
#endif
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();

		PhysObject->Initialize();
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FieldObject->BufferCommand(Solver, { TargetName, FalloffField->NewCopy() });

			R.ExpectTrue(Particles.Disabled(0));
			R.ExpectTrue(Particles.Disabled(1));
			R.ExpectTrue(Particles.Disabled(2));
			R.ExpectTrue(Particles.Disabled(3));
			R.ExpectTrue(Particles.Disabled(4));
			R.ExpectTrue(Particles.Disabled(5));
			R.ExpectTrue(!Particles.Disabled(6));

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

			R.ExpectTrue(ClusterMap.Num() == 2);
			R.ExpectTrue(ClusterMap[4]->Num() == 2);
			R.ExpectTrue(ClusterMap[4]->Contains(2));
			R.ExpectTrue(ClusterMap[4]->Contains(3));
			R.ExpectTrue(ClusterMap[5]->Num() == 2);
			R.ExpectTrue(ClusterMap[5]->Contains(0));
			R.ExpectTrue(ClusterMap[5]->Contains(1));

			R.ExpectTrue(Particles.Disabled(0));
			R.ExpectTrue(Particles.Disabled(1));
			R.ExpectTrue(Particles.Disabled(2));
			R.ExpectTrue(Particles.Disabled(3));
			R.ExpectTrue(!Particles.Disabled(4));
			R.ExpectTrue(!Particles.Disabled(5));
			R.ExpectTrue(Particles.Disabled(6));
		}
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FalloffField;

#endif

		return !R.HasError();
	}
	template bool RigidBodies_Field_ClusterBreak_StrainModel_Test1<float>(ExampleResponse&& R);



	template<class T>
	bool RigidBodies_Field_ClusterBreak_StrainModel_Test2(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_ThreeByTwo_ThreeTransform(FVector(0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1000;
			InParams.ClusterGroupIndex = 0;
			InParams.DamageThreshold = { 1 };
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};


		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.5;
		FalloffField->Radius = 200.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();
#endif
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
#endif

		PhysObject->Initialize();
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FFieldSystemCommand Command(TargetName, FalloffField->NewCopy());
			FFieldSystemMetaDataProcessingResolution* ResolutionData = new FFieldSystemMetaDataProcessingResolution(EFieldResolutionType::Field_Resolution_Maximum);
			Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution, TUniquePtr< FFieldSystemMetaDataProcessingResolution >(ResolutionData));
			FieldObject->BufferCommand(Solver, Command);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			R.ExpectTrue(Particles.Disabled(0));
			R.ExpectTrue(Particles.Disabled(1));
			R.ExpectTrue(Particles.Disabled(2));
			R.ExpectTrue(Particles.Disabled(3));
			R.ExpectTrue(Particles.Disabled(4));
			R.ExpectTrue(Particles.Disabled(5));
			R.ExpectTrue(Particles.Disabled(6));
			R.ExpectTrue(Particles.Disabled(7));
			R.ExpectTrue(!Particles.Disabled(8));
#endif

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);
			FieldObject->BufferCommand(Solver, { TargetName, FalloffField->NewCopy() });
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			R.ExpectTrue(ClusterMap.Num() == 1);
			R.ExpectTrue(ClusterMap[6]->Num() == 3);
			R.ExpectTrue(ClusterMap[6]->Contains(3));
			R.ExpectTrue(ClusterMap[6]->Contains(4));
			R.ExpectTrue(ClusterMap[6]->Contains(5));
#endif

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			R.ExpectTrue(!Particles.Disabled(0));
			R.ExpectTrue(!Particles.Disabled(1));
			R.ExpectTrue(!Particles.Disabled(2));
			R.ExpectTrue(Particles.Disabled(3));
			R.ExpectTrue(Particles.Disabled(4));
			R.ExpectTrue(Particles.Disabled(5));
			R.ExpectTrue(!Particles.Disabled(6));
			R.ExpectTrue(Particles.Disabled(7));
			R.ExpectTrue(Particles.Disabled(8));
#endif
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete PhysObject;
		delete FalloffField;
#endif

		return !R.HasError();
	}
	template bool RigidBodies_Field_ClusterBreak_StrainModel_Test2<float>(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_ClusterBreak_StrainModel_Test3(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_ThreeByTwo_ThreeTransform(FVector(0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1000;
			InParams.ClusterGroupIndex = 0;
			InParams.DamageThreshold = { 1 };
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};


		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.1;
		FalloffField->Radius = 200.0;
		FalloffField->Position = FVector(350.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();
#endif
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
#endif

		PhysObject->Initialize();
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		{

			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FFieldSystemCommand Command(TargetName, FalloffField->NewCopy());
			FFieldSystemMetaDataProcessingResolution* ResolutionData = new FFieldSystemMetaDataProcessingResolution(EFieldResolutionType::Field_Resolution_Maximum);
			Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution, TUniquePtr< FFieldSystemMetaDataProcessingResolution >(ResolutionData));
			FieldObject->BufferCommand(Solver, Command);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			R.ExpectTrue(Particles.Disabled(0));
			R.ExpectTrue(Particles.Disabled(1));
			R.ExpectTrue(Particles.Disabled(2));
			R.ExpectTrue(Particles.Disabled(3));
			R.ExpectTrue(Particles.Disabled(4));
			R.ExpectTrue(Particles.Disabled(5));
			R.ExpectTrue(Particles.Disabled(6));
			R.ExpectTrue(Particles.Disabled(7));
			R.ExpectTrue(!Particles.Disabled(8));
#endif

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			R.ExpectTrue(ClusterMap.Num() == 2);
			R.ExpectTrue(ClusterMap[6]->Num() == 3);
			R.ExpectTrue(ClusterMap[6]->Contains(3));
			R.ExpectTrue(ClusterMap[6]->Contains(4));
			R.ExpectTrue(ClusterMap[6]->Contains(5));
			R.ExpectTrue(ClusterMap[7]->Num() == 3);
			R.ExpectTrue(ClusterMap[7]->Contains(0));
			R.ExpectTrue(ClusterMap[7]->Contains(1));
			R.ExpectTrue(ClusterMap[7]->Contains(2));
#endif

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			R.ExpectTrue(Particles.Disabled(0));
			R.ExpectTrue(Particles.Disabled(1));
			R.ExpectTrue(Particles.Disabled(2));
			R.ExpectTrue(Particles.Disabled(3));
			R.ExpectTrue(Particles.Disabled(4));
			R.ExpectTrue(Particles.Disabled(5));
			R.ExpectTrue(!Particles.Disabled(6));
			R.ExpectTrue(!Particles.Disabled(7));
			R.ExpectTrue(Particles.Disabled(8));
#endif
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete PhysObject;
		delete FalloffField;
#endif

		return !R.HasError();
	}
	template bool RigidBodies_Field_ClusterBreak_StrainModel_Test3<float>(ExampleResponse&& R);


	template<class T>
	bool RigidBodies_Field_ClusterBreak_StrainModel_Test4(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_TwoByTwo_ThreeTransform(FVector(0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1000;
			InParams.ClusterGroupIndex = 0;
			InParams.DamageThreshold = { 1 };
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};


		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.5;
		FalloffField->Radius = 100.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);
		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();
#endif
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
#endif

		PhysObject->Initialize();
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FieldObject->BufferCommand(Solver, { TargetName, FalloffField->NewCopy() });

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			R.ExpectTrue(Particles.Disabled(0));
			R.ExpectTrue(Particles.Disabled(1));
			R.ExpectTrue(Particles.Disabled(2));
			R.ExpectTrue(Particles.Disabled(3));
			R.ExpectTrue(Particles.Disabled(4));
			R.ExpectTrue(Particles.Disabled(5));
			R.ExpectTrue(!Particles.Disabled(6));
#endif

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			R.ExpectTrue(ClusterMap.Num() == 2);
			R.ExpectTrue(ClusterMap[4]->Num() == 2);
			R.ExpectTrue(ClusterMap[4]->Contains(2));
			R.ExpectTrue(ClusterMap[4]->Contains(3));
			R.ExpectTrue(ClusterMap[5]->Num() == 2);
			R.ExpectTrue(ClusterMap[5]->Contains(0));
			R.ExpectTrue(ClusterMap[5]->Contains(1));
#endif

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			R.ExpectTrue(Particles.Disabled(0));
			R.ExpectTrue(Particles.Disabled(1));
			R.ExpectTrue(Particles.Disabled(2));
			R.ExpectTrue(Particles.Disabled(3));
			R.ExpectTrue(!Particles.Disabled(4));
			R.ExpectTrue(!Particles.Disabled(5));
			R.ExpectTrue(Particles.Disabled(6));
#endif
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FalloffField;
#endif

		return !R.HasError();
	}
	template bool RigidBodies_Field_ClusterBreak_StrainModel_Test4<float>(ExampleResponse&& R);


}

