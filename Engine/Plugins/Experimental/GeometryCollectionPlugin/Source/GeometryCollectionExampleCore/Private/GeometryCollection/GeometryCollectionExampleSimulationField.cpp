// Copyright Epic Games, Inc. All Rights Reserved.

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
	void RigidBodies_Field_KinematicActivation()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		//  Rigid Body Setup
		//
		const FVector Translation0(0, 0, 1);
		auto RestInitFunc = [Translation0](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->Transform[0].SetTranslation(Translation0);
		};

		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);


		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection);

		Chaos::FPBDRigidsSolver* Solver = Module->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->SetHasFloor(false);
		Solver->SetIsFloorAnalytic(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

		for (int i = 0; i < 100; i++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
		}

		FinalizeSolver(*Solver);

		// simulated
		const TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		EXPECT_EQ(Transform.Num(), 1);
		const FVector Translation1 = Transform[0].GetTranslation();
		EXPECT_EQ(Translation0, Translation1);
		EXPECT_EQ(Transform[0].GetTranslation().Z, 1.f);


		FRadialIntMask * RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, 0.0);
		RadialMask->Radius = 100.0;
		RadialMask->InteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->ExteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_IFF_NOT_Interior;
		FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState);
		PhysObject->BufferCommand(Solver, { TargetName, RadialMask });

		for (int i = 0; i < 100; i++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
		}

		FinalizeSolver(*Solver);

		const FVector Translation2 = Transform[0].GetTranslation();
		EXPECT_NE(Translation1, Translation2);
		EXPECT_LE(Transform[0].GetTranslation().Z, 0.f);

		Module->DestroySolver(Solver);
		delete PhysObject;

	}
	template void RigidBodies_Field_KinematicActivation<float>();

	template<class T>
	void RigidBodies_Field_InitialLinearVelocity()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		//  Rigid Body Setup
		//
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), nullptr, (int)EObjectStateTypeEnum::Chaos_Object_Kinematic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.InitialVelocityType = EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined;
			InParams.InitialLinearVelocity = FVector(0.f, 100.f, 0.f);
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
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetIsFloorAnalytic(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;

		float PreviousY = 0.f;
		EXPECT_EQ(Transform[0].GetTranslation().X, 0);
		EXPECT_EQ(Transform[0].GetTranslation().Y, 0);

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
				EXPECT_EQ(Transform[0].GetTranslation().X, 0);
				EXPECT_GT(Transform[0].GetTranslation().Y, PreviousY);
			}
			else
			{
				EXPECT_EQ(Transform[0].GetTranslation().X, 0);
				EXPECT_EQ(Transform[0].GetTranslation().Y, 0);
				EXPECT_EQ(Transform[0].GetTranslation().Z, 0);
			}
			PreviousY = Transform[0].GetTranslation().Y;
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete FieldObject;
		delete PhysObject;
	}
	template void RigidBodies_Field_InitialLinearVelocity<float>();

	template<class T>
	void RigidBodies_Field_StayDynamic()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		auto RestInitFunc = [](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->Transform[0].SetTranslation(FVector(0.f, 0.f, 5.f));
		};

		//
		//  Rigid Body Setup
		//
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Static };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

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
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection);

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

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
				EXPECT_LT(FMath::Abs(Transform[0].GetTranslation().Z - 5.f), SMALL_THRESHOLD);
			}
			else
			{
				EXPECT_LT(Transform[0].GetTranslation().Z, PreviousHeight);
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

	}
	template void RigidBodies_Field_StayDynamic<float>();


	template<class T>
	void RigidBodies_Field_LinearForce()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		auto RestInitFunc = [](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->Transform[0].SetTranslation(FVector(0.f, 0.f, 5.f));
		};

		//
		//  Rigid Body Setup
		//
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		//
		// Field setup
		//
		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(0.0, 1.0, 0.0);
		UniformVector->Magnitude = 1000.0;

		//
		// Solver setup
		//
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection);

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

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
				EXPECT_LT(FMath::Abs(Transform[0].GetTranslation().Y), SMALL_THRESHOLD);
			}
			else
			{
				EXPECT_GT(Transform[0].GetTranslation().Y, PreviousY);
			}

			PreviousY = Transform[0].GetTranslation().Y;

		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete UniformVector;
		delete PhysObject;
		delete FieldObject;
	}
	template void RigidBodies_Field_LinearForce<float>();

	template<class T>
	void RigidBodies_Field_Torque()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		//  Rigid Body Setup
		//
		auto RestInitFunc = [](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->Transform[0].SetTranslation(FVector(0.f, 0.f, 5.f));
		};

		InitCollectionsParameters InitParams = {FTransform::Identity, FVector(10.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic};
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		//
		// Field setup
		//
		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(0.0, 1.0, 0.0);
		UniformVector->Magnitude = 100.0;

		//
		// Solver setup
		//
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection);

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

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
				EXPECT_LT(FMath::Abs(Transform[0].GetRotation().Euler().Y), SMALL_THRESHOLD);
			}
			else
			{
				EXPECT_NE(FMath::Abs(Transform[0].GetRotation().Euler().Y), SMALL_THRESHOLD);
				EXPECT_GT(Particles.W(0).Y, PreviousY);
			}

			PreviousY = Particles.W(0).Y;
#endif

		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete UniformVector;
		delete PhysObject;
		delete FieldObject;
	}
	template void RigidBodies_Field_Torque<float>();



	template<class T>
	void RigidBodies_Field_Kill()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		auto RestInitFunc = [](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->Transform[0].SetTranslation(FVector(0.f, 0.f, 20.f));
		};

		//
		//  Rigid Body Setup
		//
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

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
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection);

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		for (int Frame = 0; Frame < 10; Frame++)
		{
			//UE_LOG(LogTest, Verbose, TEXT("Frame[%d]"), Frame);

			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_Kill);
			FieldObject->BufferCommand(Solver, { TargetName, FalloffField->NewCopy() });

			Solver->AdvanceSolverBy(1 / 24.);

			FinalizeSolver(*Solver);

			EXPECT_LT(Transform[0].GetTranslation().Z, 20.f);
			EXPECT_GT(Transform[0].GetTranslation().Z, -10.);
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FieldObject;
		delete FalloffField;

	}
	template void RigidBodies_Field_Kill<float>();

	template<class T>
	void RigidBodies_Field_LinearVelocity()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		auto RestInitFunc = [](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->Transform[0].SetTranslation(FVector(0.f, 0.f, 20.f));
		};

		//
		//  Rigid Body Setup
		//
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		//
		// Field setup
		//
		FUniformVector * VectorField = new FUniformVector();
		VectorField->Magnitude = 100.0;
		VectorField->Direction = FVector(1.0, 0.0, 0.0);

		//
		// Solver setup
		//
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection);

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

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

			EXPECT_GT(Transform[0].GetTranslation().X, PreviousX);
			PreviousX = Transform[0].GetTranslation().X;
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FieldObject;
		delete VectorField;

	}
	template void RigidBodies_Field_LinearVelocity<float>();


	/**
	 * Create a stack of boxes on the ground and verify that we we change their collision
	 * group, they drop through the ground.
	 */
	template<class T>
	void RigidBodies_Field_CollisionGroup()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		//
		// Generate Geometry - a stack of boxes.
		// The bottom box is on the ground, and the others are dropped into it.
		//
		auto RestInitFunc = [](TSharedPtr<FGeometryCollection>& RestCollection)
		{
			RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 400)), FVector(100)));
			RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 600)), FVector(100)));
			RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FVector(0, 0, 800)), FVector(100)));
		};

		InitCollectionsParameters InitParams = { FTransform(FVector(0, 0, 100)), FVector(200), RestInitFunc, (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic };

		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

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
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		for (int Frame = 0; Frame < 60; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

			Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
			if (Frame == 30)
			{
				// The boxes should have landed on each other and settled by now
				EXPECT_LT(FMath::Abs(Particles.X(0).Z), SMALL_NUMBER);
				EXPECT_TRUE(FMath::IsNearlyEqual(Particles.X(1).Z, (T)100, (T)2));
				EXPECT_TRUE(FMath::IsNearlyEqual(Particles.X(2).Z, (T)250, (T)2));
				EXPECT_TRUE(FMath::IsNearlyEqual(Particles.X(3).Z, (T)350, (T)2));
				EXPECT_TRUE(FMath::IsNearlyEqual(Particles.X(4).Z, (T)450, (T)2));
			}
			if (Frame == 31)
			{
				FName TargetName = GetGeometryCollectionPhysicsTypeName(EGeometryCollectionPhysicsTypeEnum::Chaos_CollisionGroup);
				PhysObject->BufferCommand(Solver, { TargetName, RadialMask });
			}
		}
		// The boxes should have fallen below the ground level
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
		EXPECT_LT(FMath::Abs(Particles.X(0).Z), SMALL_NUMBER);
		EXPECT_LT(Particles.X(1).Z, 0);
		EXPECT_LT(Particles.X(2).Z, 0);
		EXPECT_LT(Particles.X(3).Z, 0);
		EXPECT_LT(Particles.X(4).Z, 0);
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete RadialMask;
	}
	template void RigidBodies_Field_CollisionGroup<float>();

	template<class T>
	void RigidBodies_Field_ClusterBreak_StrainModel_Test1()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_TwoByTwo_ThreeTransform(FVector(0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		// FTransform::Identity and FVector(0.0) are defaults - they won't be used because RestCollection is already initialized. 
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(0.0), nullptr, (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);
		
		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.ClusterConnectionMethod = Chaos::FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation;
			InParams.MaxClusterLevel = 1000;
			InParams.ClusterGroupIndex = 0;
			InParams.DamageThreshold = { 1 };
		};

		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.5;
		FalloffField->Radius = 100.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();
#endif
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();

		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FieldObject->BufferCommand(Solver, { TargetName, FalloffField->NewCopy() });

			EXPECT_TRUE(Particles.Disabled(0));
			EXPECT_TRUE(Particles.Disabled(1));
			EXPECT_TRUE(Particles.Disabled(2));
			EXPECT_TRUE(Particles.Disabled(3));
			EXPECT_TRUE(Particles.Disabled(4));
			EXPECT_TRUE(Particles.Disabled(5));
			EXPECT_FALSE(Particles.Disabled(6));

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

			EXPECT_EQ(ClusterMap.Num(), 2);
			EXPECT_EQ(ClusterMap[4]->Num(), 2);
			EXPECT_TRUE(ClusterMap[4]->Contains(2));
			EXPECT_TRUE(ClusterMap[4]->Contains(3));
			EXPECT_EQ(ClusterMap[5]->Num(), 2);
			EXPECT_TRUE(ClusterMap[5]->Contains(0));
			EXPECT_TRUE(ClusterMap[5]->Contains(1));

			EXPECT_TRUE(Particles.Disabled(0));
			EXPECT_TRUE(Particles.Disabled(1));
			EXPECT_TRUE(Particles.Disabled(2));
			EXPECT_TRUE(Particles.Disabled(3));
			EXPECT_FALSE(Particles.Disabled(4));
			EXPECT_FALSE(Particles.Disabled(5));
			EXPECT_TRUE(Particles.Disabled(6));
		}
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FalloffField;
		delete FieldObject;
	}
	template void RigidBodies_Field_ClusterBreak_StrainModel_Test1<float>();



	template<class T>
	void RigidBodies_Field_ClusterBreak_StrainModel_Test2()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_ThreeByTwo_ThreeTransform(FVector(0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		// FTransform::Identity and FVector(0.0) are defaults - they won't be used because RestCollection is already initialized. 
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(0.0), nullptr, (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1000;
			InParams.ClusterGroupIndex = 0;
			InParams.DamageThreshold = { 1 };
		};

		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.5;
		FalloffField->Radius = 200.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);
		
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();
#endif
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
#endif

		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FFieldSystemCommand Command(TargetName, FalloffField->NewCopy());
			FFieldSystemMetaDataProcessingResolution* ResolutionData = new FFieldSystemMetaDataProcessingResolution(EFieldResolutionType::Field_Resolution_Maximum);
			Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution, TUniquePtr< FFieldSystemMetaDataProcessingResolution >(ResolutionData));
			FieldObject->BufferCommand(Solver, Command);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			EXPECT_TRUE(Particles.Disabled(0));
			EXPECT_TRUE(Particles.Disabled(1));
			EXPECT_TRUE(Particles.Disabled(2));
			EXPECT_TRUE(Particles.Disabled(3));
			EXPECT_TRUE(Particles.Disabled(4));
			EXPECT_TRUE(Particles.Disabled(5));
			EXPECT_TRUE(Particles.Disabled(6));
			EXPECT_TRUE(Particles.Disabled(7));
			EXPECT_FALSE(Particles.Disabled(8));
#endif

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);
			FieldObject->BufferCommand(Solver, { TargetName, FalloffField->NewCopy() });
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			EXPECT_EQ(ClusterMap.Num(), 1);
			EXPECT_EQ(ClusterMap[6]->Num(), 3);
			EXPECT_TRUE(ClusterMap[6]->Contains(3));
			EXPECT_TRUE(ClusterMap[6]->Contains(4));
			EXPECT_TRUE(ClusterMap[6]->Contains(5));
#endif

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			EXPECT_FALSE(Particles.Disabled(0));
			EXPECT_FALSE(Particles.Disabled(1));
			EXPECT_FALSE(Particles.Disabled(2));
			EXPECT_TRUE(Particles.Disabled(3));
			EXPECT_TRUE(Particles.Disabled(4));
			EXPECT_TRUE(Particles.Disabled(5));
			EXPECT_FALSE(Particles.Disabled(6));
			EXPECT_TRUE(Particles.Disabled(7));
			EXPECT_TRUE(Particles.Disabled(8));
#endif
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete PhysObject;
		delete FalloffField;
		delete FieldObject;
	}
	template void RigidBodies_Field_ClusterBreak_StrainModel_Test2<float>();

	template<class T>
	void RigidBodies_Field_ClusterBreak_StrainModel_Test3()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_ThreeByTwo_ThreeTransform(FVector(0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		// FTransform::Identity and FVector(0.0) are defaults - they won't be used because RestCollection is already initialized. 
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(0.0), nullptr, (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1000;
			InParams.ClusterGroupIndex = 0;
			InParams.DamageThreshold = { 1 };
		};

		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.1;
		FalloffField->Radius = 200.0;
		FalloffField->Position = FVector(350.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);
		
		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();
#endif
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
#endif
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		{

			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FFieldSystemCommand Command(TargetName, FalloffField->NewCopy());
			FFieldSystemMetaDataProcessingResolution* ResolutionData = new FFieldSystemMetaDataProcessingResolution(EFieldResolutionType::Field_Resolution_Maximum);
			Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution, TUniquePtr< FFieldSystemMetaDataProcessingResolution >(ResolutionData));
			FieldObject->BufferCommand(Solver, Command);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			EXPECT_TRUE(Particles.Disabled(0));
			EXPECT_TRUE(Particles.Disabled(1));
			EXPECT_TRUE(Particles.Disabled(2));
			EXPECT_TRUE(Particles.Disabled(3));
			EXPECT_TRUE(Particles.Disabled(4));
			EXPECT_TRUE(Particles.Disabled(5));
			EXPECT_TRUE(Particles.Disabled(6));
			EXPECT_TRUE(Particles.Disabled(7));
			EXPECT_FALSE(Particles.Disabled(8));
#endif

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			EXPECT_EQ(ClusterMap.Num(), 2);
			EXPECT_EQ(ClusterMap[6]->Num(), 3);
			EXPECT_TRUE(ClusterMap[6]->Contains(3));
			EXPECT_TRUE(ClusterMap[6]->Contains(4));
			EXPECT_TRUE(ClusterMap[6]->Contains(5));
			EXPECT_EQ(ClusterMap[7]->Num(), 3);
			EXPECT_TRUE(ClusterMap[7]->Contains(0));
			EXPECT_TRUE(ClusterMap[7]->Contains(1));
			EXPECT_TRUE(ClusterMap[7]->Contains(2));
#endif

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			EXPECT_TRUE(Particles.Disabled(0));
			EXPECT_TRUE(Particles.Disabled(1));
			EXPECT_TRUE(Particles.Disabled(2));
			EXPECT_TRUE(Particles.Disabled(3));
			EXPECT_TRUE(Particles.Disabled(4));
			EXPECT_TRUE(Particles.Disabled(5));
			EXPECT_FALSE(Particles.Disabled(6));
			EXPECT_FALSE(Particles.Disabled(7));
			EXPECT_TRUE(Particles.Disabled(8));
#endif
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete PhysObject;
		delete FalloffField;
		delete FieldObject;
	}
	template void RigidBodies_Field_ClusterBreak_StrainModel_Test3<float>();


	template<class T>
	void RigidBodies_Field_ClusterBreak_StrainModel_Test4()
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = nullptr;
		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_TwoByTwo_ThreeTransform(FVector(0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

		// FTransform::Identity and FVector(0.0) are defaults - they won't be used because RestCollection is already initialized. 
		InitCollectionsParameters InitParams = { FTransform::Identity, FVector(0.0), nullptr, (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1000;
			InParams.ClusterGroupIndex = 0;
			InParams.DamageThreshold = { 1 };
		};

		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.5;
		FalloffField->Radius = 100.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();
#endif
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
#endif
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AddDirtyProxy(PhysObject);
		Solver->AddDirtyProxy(FieldObject);
		Solver->PushPhysicsState(Module->GetDispatcher());

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FieldObject->BufferCommand(Solver, { TargetName, FalloffField->NewCopy() });

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			EXPECT_TRUE(Particles.Disabled(0));
			EXPECT_TRUE(Particles.Disabled(1));
			EXPECT_TRUE(Particles.Disabled(2));
			EXPECT_TRUE(Particles.Disabled(3));
			EXPECT_TRUE(Particles.Disabled(4));
			EXPECT_TRUE(Particles.Disabled(5));
			EXPECT_FALSE(Particles.Disabled(6));
#endif

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			EXPECT_EQ(ClusterMap.Num(), 2);
			EXPECT_EQ(ClusterMap[4]->Num(), 2);
			EXPECT_TRUE(ClusterMap[4]->Contains(2));
			EXPECT_TRUE(ClusterMap[4]->Contains(3));
			EXPECT_EQ(ClusterMap[5]->Num(), 2);
			EXPECT_TRUE(ClusterMap[5]->Contains(0));
			EXPECT_TRUE(ClusterMap[5]->Contains(1));
#endif

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			EXPECT_TRUE(Particles.Disabled(0));
			EXPECT_TRUE(Particles.Disabled(1));
			EXPECT_TRUE(Particles.Disabled(2));
			EXPECT_TRUE(Particles.Disabled(3));
			EXPECT_FALSE(Particles.Disabled(4));
			EXPECT_FALSE(Particles.Disabled(5));
			EXPECT_TRUE(Particles.Disabled(6));
#endif
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FalloffField;
		delete FieldObject;
	}
	template void RigidBodies_Field_ClusterBreak_StrainModel_Test4<float>();

}
