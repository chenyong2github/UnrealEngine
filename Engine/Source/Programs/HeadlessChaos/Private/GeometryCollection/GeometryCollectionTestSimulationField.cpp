// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionTestSimulationField.h"
#include "GeometryCollection/GeometryCollectionTestUtility.h"
#include "GeometryCollection/GeometryCollectionTestFramework.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/TransformCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "Field/FieldSystem.h"
#include "Field/FieldSystemNodes.h"
#include "GeometryCollectionProxyData.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/PBDRigidClustering.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"
#include "HeadlessChaosTestUtility.h"

//#include "GeometryCollection/GeometryCollectionAlgo.h"

#define SMALL_THRESHOLD 1e-4

// #TODO Lots of duplication in here, anyone making solver or object changes
// has to go and fix up so many callsites here and they're all pretty much
// Identical. The similar code should be pulled out

namespace GeometryCollectionTest
{
	using namespace ChaosTest;

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_KinematicActivationOnProxyDuringInit)
	{
		using Traits = TypeParam;
		const FVector Translation0(0, 0, 1);

		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		Params.RootTransform.SetLocation(Translation0);
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		Params.RootTransform.SetLocation(FVector(100,0,0));
		TGeometryCollectionWrapper<Traits>* CollectionOther = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		TFramework<Traits> UnitTest;
		UnitTest.AddSimulationObject(CollectionOther);
		UnitTest.AddSimulationObject(Collection);

		FRadialIntMask* RadialMaskTmp = new FRadialIntMask();
		RadialMaskTmp->Position = FVector(0.0, 0.0, 0.0);
		RadialMaskTmp->Radius = 100.0;
		RadialMaskTmp->InteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMaskTmp->ExteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		RadialMaskTmp->SetMaskCondition = ESetMaskConditionType::Field_Set_IFF_NOT_Interior;
		FName TargetNameTmp = GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState);
		Collection->PhysObject->BufferCommand(UnitTest.Solver, { TargetNameTmp, RadialMaskTmp });

		UnitTest.Initialize();

		UnitTest.Solver->RegisterSimOneShotCallback([&]()
		{
			EXPECT_EQ(CollectionOther->DynamicCollection->DynamicState[0],(int32)EObjectStateTypeEnum::Chaos_Object_Kinematic);
			EXPECT_EQ(Collection->DynamicCollection->DynamicState[0],(int32)EObjectStateTypeEnum::Chaos_Object_Dynamic);
		});
	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_KinematicActivationOnProxyDuringUpdate)
	{
		using Traits = TypeParam;
		const FVector Translation0(0, 0, 1);

		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		Params.RootTransform.SetLocation(Translation0);
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		TFramework<Traits> UnitTest; UnitTest.Dt = 1/24.0;
		UnitTest.AddSimulationObject(Collection);

		UnitTest.Initialize();

		{
			UnitTest.Advance();
		}

		TManagedArray<int32>& DynamicState = Collection->DynamicCollection->DynamicState;
		EXPECT_EQ(DynamicState[0], (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic);

		// simulated
		const TManagedArray<FTransform>& Transform = Collection->DynamicCollection->Transform;
		EXPECT_EQ(Transform.Num(), 1);
		const FVector Translation1 = Transform[0].GetTranslation();
		EXPECT_NEAR( (Translation0-Translation1).Size(), 0.f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(Transform[0].GetTranslation().Z, 1.f, KINDA_SMALL_NUMBER);

		FRadialIntMask* RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, 0.0);
		RadialMask->Radius = 100.0;
		RadialMask->InteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->ExteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_IFF_NOT_Interior;
		FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState);
		Collection->PhysObject->BufferCommand(UnitTest.Solver, { TargetName, RadialMask });

		{
			UnitTest.Advance();
		}
		EXPECT_EQ(DynamicState[0], (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		const FVector Translation2 = Transform[0].GetTranslation();
		EXPECT_NE(Translation1, Translation2);
		EXPECT_LE(Transform[0].GetTranslation().Z, 0.f);
	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_KinematicActivation)
	{
		using Traits = TypeParam;
		const FVector Translation0(0, 0, 1);

		CreationParameters Params; 
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		Params.RootTransform.SetLocation(Translation0);
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		TFramework<Traits> UnitTest;
		UnitTest.AddSimulationObject(Collection);

		UnitTest.Initialize();

		for (int i = 0; i < 100; i++)
		{
			UnitTest.Advance();
		}

		// simulated
		const TManagedArray<FTransform>& Transform = Collection->DynamicCollection->Transform;
		EXPECT_EQ(Transform.Num(), 1);
		const FVector Translation1 = Transform[0].GetTranslation();
		EXPECT_NEAR( (Translation0-Translation1).Size(), 0.f, KINDA_SMALL_NUMBER);
		EXPECT_NEAR(Transform[0].GetTranslation().Z, 1.f, KINDA_SMALL_NUMBER);

		FRadialIntMask* RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, 0.0);
		RadialMask->Radius = 100.0;
		RadialMask->InteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->ExteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_IFF_NOT_Interior;
		FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState);
		UnitTest.Solver->GetPerSolverField().BufferCommand( { TargetName, RadialMask });

		for (int i = 0; i < 100; i++)
		{
			UnitTest.Advance();
		}

		const FVector Translation2 = Transform[0].GetTranslation();
		EXPECT_NE(Translation1, Translation2);
		EXPECT_LE(Transform[0].GetTranslation().Z, 0.f);
	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_InitialLinearVelocity)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		// Physics Object Setup
		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		Params.RootTransform.SetLocation(FVector(0.0, 0.0, 0.0));

		Params.InitialVelocityType = EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined;
		Params.InitialLinearVelocity = FVector(0.f, 100.f, 0.f);
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(Collection);

		// Field setup
		FRadialIntMask* RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, 0.0);
		RadialMask->Radius = 5.0f;
		RadialMask->InteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->ExteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_Always;

		UnitTest.Initialize();

		TManagedArray<FTransform>& Transform = Collection->DynamicCollection->Transform;
		TManagedArray<int32>& DynamicState = Collection->DynamicCollection->DynamicState;

		float PreviousY = 0.f;
		EXPECT_EQ(Transform[0].GetTranslation().X, 0);
		EXPECT_EQ(Transform[0].GetTranslation().Y, 0);

		for (int Frame = 0; Frame < 10; Frame++)
		{
			UnitTest.Advance();

			if (Frame == 1)
			{
				FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState);
				Collection->PhysObject->BufferCommand(UnitTest.Solver, { TargetName, RadialMask });
			}

			if (Frame >= 2)
			{
				EXPECT_EQ(DynamicState[0], (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic);
				EXPECT_EQ(Transform[0].GetTranslation().X, 0);
				EXPECT_GT(Transform[0].GetTranslation().Y, PreviousY);
				EXPECT_LT(Transform[0].GetTranslation().Z, 0);
			}
			else
			{
				EXPECT_EQ(DynamicState[0], (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic);
				EXPECT_EQ(Transform[0].GetTranslation().X, 0);
				EXPECT_EQ(Transform[0].GetTranslation().Y, 0);
				EXPECT_EQ(Transform[0].GetTranslation().Z, 0);
			}
			PreviousY = Transform[0].GetTranslation().Y;
		}
	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_StayDynamic)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;
		float PreviousHeight = 5.f;

		// Physics Object Setup
		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Static;
		Params.RootTransform.SetLocation(FVector(0.f, 0.f, PreviousHeight));
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(Collection);

		// Field setup
		FRadialIntMask * RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, PreviousHeight);
		RadialMask->Radius = 5.0;
		RadialMask->InteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
		RadialMask->ExteriorValue = (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_IFF_NOT_Interior;

		UnitTest.Initialize();

		TManagedArray<FTransform>& Transform = Collection->DynamicCollection->Transform;
		for (int Frame = 0; Frame < 10; Frame++)
		{
			// Set everything inside the r=5.0 sphere to dynamic
			if (Frame == 5)
			{
				FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState);
				UnitTest.Solver->GetPerSolverField().BufferCommand( { TargetName, RadialMask });
			}

			UnitTest.Advance();

			if (Frame < 5)
			{
				// Before frame 5 nothing should have moved
				EXPECT_LT(FMath::Abs(Transform[0].GetTranslation().Z - 5.f), SMALL_THRESHOLD);
			}
			else
			{
				// Frame 5 and after should be falling
				EXPECT_LT(Transform[0].GetTranslation().Z, PreviousHeight);
			}

			// Track current height of the object
			PreviousHeight = Transform[0].GetTranslation().Z;
		}

	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_LinearForce)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		// Physics Object Setup
		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.RootTransform.SetLocation(FVector(0.f, 0.f, 5.f));
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(Collection);

		// Field setup
		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(0.0, 1.0, 0.0);
		UniformVector->Magnitude = 1000.0;

		UnitTest.Initialize();

		TManagedArray<FTransform>& Transform = Collection->DynamicCollection->Transform;
		float PreviousY = 0.f;
		for (int Frame = 0; Frame < 10; Frame++)
		{
			if (Frame >= 5)
			{
				FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_LinearForce);
				UnitTest.Solver->GetPerSolverField().BufferCommand( { TargetName, UniformVector->NewCopy() });
			}

			UnitTest.Advance();

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

		delete UniformVector;
	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_Torque)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		// Physics Object Setup
		FVector Scale = FVector(10.0);
		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.RootTransform.SetLocation(FVector(0.f, 0.f, 5.f));
		Params.GeomTransform.SetScale3D(Scale);
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(Collection);

		// Field setup
		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(0.0, 1.0, 0.0);
		UniformVector->Magnitude = 100.0;

		UnitTest.Initialize();

		TManagedArray<FTransform>& Transform = Collection->DynamicCollection->Transform;
		float PreviousY = 0.f;
		for (int Frame = 0; Frame < 10; Frame++)
		{
			if (Frame >= 5)
			{
				FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_AngularTorque);
				UnitTest.Solver->GetPerSolverField().BufferCommand( { TargetName, UniformVector->NewCopy() });
			}

			UnitTest.Advance();

			auto& Particles = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles();
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
		}

	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_Kill)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		// Physics Object Setup
		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.RootTransform.SetLocation(FVector(0.f, 0.f, 20.f));
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(Collection);

		// Field setup
		FPlaneFalloff * FalloffField = new FPlaneFalloff();
		FalloffField->Magnitude = 1.0;
		FalloffField->Distance = 10.0f;
		FalloffField->Position = FVector(0.0, 0.0, 5.0);
		FalloffField->Normal = FVector(0.0, 0.0, 1.0);
		FalloffField->Falloff = EFieldFalloffType::Field_Falloff_Linear;

		UnitTest.Initialize();

		TManagedArray<FTransform>& Transform = Collection->DynamicCollection->Transform;
		TManagedArray<bool>& Active = Collection->DynamicCollection->Active;
		auto& Particles = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles();
		for(int Frame = 0; Frame < 20; Frame++)
		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_Kill);
			UnitTest.Solver->GetPerSolverField().BufferCommand( { TargetName, FalloffField->NewCopy() });

			UnitTest.Advance();

			if(Particles.Disabled(0))
			{
				break;
			}
		}

		EXPECT_EQ(Particles.Disabled(0), true);

		// hasn't fallen any further than this due to being disabled
		EXPECT_LT(Transform[0].GetTranslation().Z, 5.f);
		EXPECT_GT(Transform[0].GetTranslation().Z, -5.0f);
	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_LinearVelocity)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		// Physics Object Setup
		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.RootTransform.SetLocation(FVector(0.f, 0.f, 20.f));
		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(Collection);

		// Field setup
		FUniformVector * VectorField = new FUniformVector();
		VectorField->Magnitude = 100.0;
		VectorField->Direction = FVector(1.0, 0.0, 0.0);

		UnitTest.Initialize();

		FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity);
		UnitTest.Solver->GetPerSolverField().BufferCommand( { TargetName, VectorField->NewCopy() });
		UnitTest.Advance();

		float PreviousX = 0.f;
		TManagedArray<FTransform>& Transform = Collection->DynamicCollection->Transform;
		for (int Frame = 1; Frame < 10; Frame++)
		{
			UnitTest.Solver->GetPerSolverField().BufferCommand( { GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity), VectorField->NewCopy() });

			UnitTest.Advance();

			EXPECT_GT(Transform[0].GetTranslation().X, PreviousX);
			PreviousX = Transform[0].GetTranslation().X;
		}
	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_CollisionGroup)
	{
		/**
		 * Create a stack of boxes on the ground and verify that we we change their collision
		 * group, they drop through the ground.
		 */

		using Traits = TypeParam;
		TFramework<Traits> UnitTest; UnitTest.Dt = 1/24.0;

		RigidBodyWrapper* Floor = TNewSimulationObject<GeometryType::RigidFloor>::Init<Traits>()->template As<RigidBodyWrapper>();
		UnitTest.AddSimulationObject(Floor);

		// Generate Geometry - a stack of boxes.
		// The bottom box is on the ground, and the others are dropped into it.
		CreationParameters Params;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		FVector Scale = FVector(100);
		Params.GeomTransform.SetScale3D(Scale);

		TGeometryCollectionWrapper<Traits>* Collection[4];
		for (int n=0; n<3;n++)
		{
			Params.RootTransform.SetLocation(FVector(0.f, 0.f, n*200.0f + 100.0f));
			Params.CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			Collection[n+1] = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
			UnitTest.AddSimulationObject(Collection[n+1]);
		}

		// Field setup
		FRadialIntMask * RadialMask = new FRadialIntMask();
		RadialMask->Position = FVector(0.0, 0.0, 0.0);
		RadialMask->Radius = 0;
		RadialMask->InteriorValue = -1;
		RadialMask->ExteriorValue = -1;
		RadialMask->SetMaskCondition = ESetMaskConditionType::Field_Set_Always;

		UnitTest.Initialize();

		for (int Frame = 0; Frame < 60; Frame++)
		{
			UnitTest.Advance();

			auto& Particles = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles();
			if (Frame == 30)
			{
				// The boxes should have landed on each other and settled by now
				EXPECT_NEAR(Collection[1]->DynamicCollection->Transform[0].GetTranslation().Z, (FReal)100, (FReal)20);
				EXPECT_NEAR(Collection[2]->DynamicCollection->Transform[0].GetTranslation().Z, (FReal)300, (FReal)20);
				EXPECT_NEAR(Collection[3]->DynamicCollection->Transform[0].GetTranslation().Z, (FReal)500, (FReal)20);
			}
			if (Frame == 31)
			{
				FName TargetName = GetGeometryCollectionPhysicsTypeName(EGeometryCollectionPhysicsTypeEnum::Chaos_CollisionGroup);
				Collection[1]->PhysObject->BufferCommand(UnitTest.Solver, { TargetName, RadialMask });
			}
		}
		// The bottom boxes should have fallen below the ground level, box 2 now on the ground with box 3 on top
		auto& Particles = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles();
		EXPECT_LT(Collection[1]->DynamicCollection->Transform[0].GetTranslation().Z, 0);
		EXPECT_TRUE(FMath::IsNearlyEqual(Collection[2]->DynamicCollection->Transform[0].GetTranslation().Z, (FReal)100, (FReal)20));
		EXPECT_TRUE(FMath::IsNearlyEqual(Collection[3]->DynamicCollection->Transform[0].GetTranslation().Z, (FReal)300, (FReal)20));

	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_ClusterBreak_StrainModel_Test1)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_TwoByTwo_ThreeTransform(FVector(0));

		CreationParameters Params;
		Params.RestCollection = RestCollection;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		Params.Simulating = true;
		Params.EnableClustering = true;
		Params.DamageThreshold = { 1.0 };
		Params.MaxClusterLevel = 1000;
		Params.ClusterConnectionMethod = Chaos::FClusterCreationParameters<FReal>::EConnectionMethod::DelaunayTriangulation;
		Params.ClusterGroupIndex = 0;

		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSuppliedRestCollection>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		UnitTest.AddSimulationObject(Collection);

		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.5;
		FalloffField->Radius = 100.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;
		
		UnitTest.Initialize();		

		auto& Clustering = UnitTest.Solver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();

		UnitTest.Advance();
		
		TArray<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*>& ParticleHandles = Collection->PhysObject->GetSolverParticleHandles();
		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			UnitTest.Solver->GetPerSolverField().BufferCommand( { TargetName, FalloffField->NewCopy() });

			EXPECT_EQ(ClusterMap.Num(), 3);
			EXPECT_EQ(ClusterMap[ParticleHandles[4]].Num(), 2);
			EXPECT_TRUE(ClusterMap[ParticleHandles[4]].Contains(ParticleHandles[0]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[4]].Contains(ParticleHandles[1]));
			EXPECT_EQ(ClusterMap[ParticleHandles[5]].Num(), 2);
			EXPECT_TRUE(ClusterMap[ParticleHandles[5]].Contains(ParticleHandles[2]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[5]].Contains(ParticleHandles[3]));
			EXPECT_EQ(ClusterMap[ParticleHandles[6]].Num(), 2);
			EXPECT_TRUE(ClusterMap[ParticleHandles[6]].Contains(ParticleHandles[5]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[6]].Contains(ParticleHandles[4]));

			EXPECT_TRUE(ParticleHandles[0]->Disabled());
			EXPECT_TRUE(ParticleHandles[1]->Disabled());
			EXPECT_TRUE(ParticleHandles[2]->Disabled());
			EXPECT_TRUE(ParticleHandles[3]->Disabled());
			EXPECT_TRUE(ParticleHandles[4]->Disabled());
			EXPECT_TRUE(ParticleHandles[5]->Disabled());
			EXPECT_FALSE(ParticleHandles[6]->Disabled());			

			UnitTest.Advance();			

			// todo: indices here might seem odd, particles 4 & 5 are swapped
			EXPECT_EQ(ClusterMap.Num(), 2);
			EXPECT_EQ(ClusterMap[ParticleHandles[4]].Num(), 2);
			EXPECT_TRUE(ClusterMap[ParticleHandles[4]].Contains(ParticleHandles[0]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[4]].Contains(ParticleHandles[1]));
			EXPECT_EQ(ClusterMap[ParticleHandles[5]].Num(), 2);
			EXPECT_TRUE(ClusterMap[ParticleHandles[5]].Contains(ParticleHandles[2]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[5]].Contains(ParticleHandles[3]));

			EXPECT_TRUE(ParticleHandles[0]->Disabled());
			EXPECT_TRUE(ParticleHandles[1]->Disabled());
			EXPECT_TRUE(ParticleHandles[2]->Disabled());
			EXPECT_TRUE(ParticleHandles[3]->Disabled());
			EXPECT_FALSE(ParticleHandles[4]->Disabled());
			EXPECT_FALSE(ParticleHandles[5]->Disabled());
			EXPECT_TRUE(ParticleHandles[6]->Disabled());
		}

		delete FalloffField;
	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_ClusterBreak_StrainModel_Test2)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_ThreeByTwo_ThreeTransform(FVector(0));

		CreationParameters Params;
		Params.RestCollection = RestCollection;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		Params.Simulating = true;
		Params.EnableClustering = true;
		Params.DamageThreshold = { 1.0 };
		Params.MaxClusterLevel = 1000;		
		Params.ClusterGroupIndex = 0;

		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSuppliedRestCollection>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		UnitTest.AddSimulationObject(Collection);

		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.5;
		FalloffField->Radius = 200.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		UnitTest.Initialize();	
		UnitTest.Advance();

		TArray<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*>& ParticleHandles = Collection->PhysObject->GetSolverParticleHandles();
		auto& Clustering = UnitTest.Solver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();

		{	
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FFieldSystemCommand Command(TargetName, FalloffField->NewCopy());
			FFieldSystemMetaDataProcessingResolution* ResolutionData = new FFieldSystemMetaDataProcessingResolution(EFieldResolutionType::Field_Resolution_Maximum);
			Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution, TUniquePtr< FFieldSystemMetaDataProcessingResolution >(ResolutionData));
			UnitTest.Solver->GetPerSolverField().BufferCommand( Command);

			EXPECT_EQ(ClusterMap.Num(), 3);
			EXPECT_EQ(ClusterMap[ParticleHandles[6]].Num(), 3);
			EXPECT_TRUE(ClusterMap[ParticleHandles[6]].Contains(ParticleHandles[0]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[6]].Contains(ParticleHandles[1]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[6]].Contains(ParticleHandles[2]));
			EXPECT_EQ(ClusterMap[ParticleHandles[7]].Num(), 3);
			EXPECT_TRUE(ClusterMap[ParticleHandles[7]].Contains(ParticleHandles[3]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[7]].Contains(ParticleHandles[4]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[7]].Contains(ParticleHandles[5]));
			EXPECT_EQ(ClusterMap[ParticleHandles[8]].Num(), 2);
			EXPECT_TRUE(ClusterMap[ParticleHandles[8]].Contains(ParticleHandles[7]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[8]].Contains(ParticleHandles[6]));

			EXPECT_TRUE(ParticleHandles[0]->Disabled());
			EXPECT_TRUE(ParticleHandles[1]->Disabled());
			EXPECT_TRUE(ParticleHandles[2]->Disabled());
			EXPECT_TRUE(ParticleHandles[3]->Disabled());
			EXPECT_TRUE(ParticleHandles[4]->Disabled());
			EXPECT_TRUE(ParticleHandles[5]->Disabled());
			EXPECT_TRUE(ParticleHandles[6]->Disabled());
			EXPECT_TRUE(ParticleHandles[7]->Disabled());
			EXPECT_FALSE(ParticleHandles[8]->Disabled());

			UnitTest.Advance();
			UnitTest.Solver->GetPerSolverField().BufferCommand( Command);
			UnitTest.Advance();

			EXPECT_EQ(ClusterMap.Num(), 1);
			EXPECT_EQ(ClusterMap[ParticleHandles[7]].Num(), 3);
			EXPECT_TRUE(ClusterMap[ParticleHandles[7]].Contains(ParticleHandles[3]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[7]].Contains(ParticleHandles[4]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[7]].Contains(ParticleHandles[5]));

			EXPECT_FALSE(ParticleHandles[0]->Disabled());
			EXPECT_FALSE(ParticleHandles[1]->Disabled());
			EXPECT_FALSE(ParticleHandles[2]->Disabled());
			EXPECT_TRUE(ParticleHandles[3]->Disabled());
			EXPECT_TRUE(ParticleHandles[4]->Disabled());
			EXPECT_TRUE(ParticleHandles[5]->Disabled());
			EXPECT_TRUE(ParticleHandles[6]->Disabled());
			EXPECT_FALSE(ParticleHandles[7]->Disabled());
			EXPECT_TRUE(ParticleHandles[8]->Disabled());
		}

		delete FalloffField;
	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_ClusterBreak_StrainModel_Test3)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_ThreeByTwo_ThreeTransform(FVector(0));

		CreationParameters Params;
		Params.RestCollection = RestCollection;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		Params.Simulating = true;
		Params.EnableClustering = true;
		Params.DamageThreshold = { 1.0 };
		Params.MaxClusterLevel = 1000;		
		Params.ClusterGroupIndex = 0;

		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSuppliedRestCollection>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		UnitTest.AddSimulationObject(Collection);

		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.1;
		FalloffField->Radius = 200.0;
		FalloffField->Position = FVector(350.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		UnitTest.Initialize();
		UnitTest.Advance();

		auto& Clustering = UnitTest.Solver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();

		TArray<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*>& ParticleHandles = Collection->PhysObject->GetSolverParticleHandles();

		UnitTest.Advance();

		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			FFieldSystemCommand Command(TargetName, FalloffField->NewCopy());
			FFieldSystemMetaDataProcessingResolution* ResolutionData = new FFieldSystemMetaDataProcessingResolution(EFieldResolutionType::Field_Resolution_Maximum);
			Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution, TUniquePtr< FFieldSystemMetaDataProcessingResolution >(ResolutionData));
			UnitTest.Solver->GetPerSolverField().BufferCommand( Command);
		
			EXPECT_TRUE(ParticleHandles[0]->Disabled());
			EXPECT_TRUE(ParticleHandles[1]->Disabled());
			EXPECT_TRUE(ParticleHandles[2]->Disabled());
			EXPECT_TRUE(ParticleHandles[3]->Disabled());
			EXPECT_TRUE(ParticleHandles[4]->Disabled());
			EXPECT_TRUE(ParticleHandles[5]->Disabled());
			EXPECT_TRUE(ParticleHandles[6]->Disabled());
			EXPECT_TRUE(ParticleHandles[7]->Disabled());
			EXPECT_FALSE(ParticleHandles[8]->Disabled());

			UnitTest.Advance();

			// todo: indices here might be off but the test crashes before this so we can't validate yet
			EXPECT_EQ(ClusterMap.Num(), 2);
			EXPECT_EQ(ClusterMap[ParticleHandles[7]].Num(), 3);
			EXPECT_TRUE(ClusterMap[ParticleHandles[7]].Contains(ParticleHandles[3]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[7]].Contains(ParticleHandles[4]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[7]].Contains(ParticleHandles[5]));
			EXPECT_EQ(ClusterMap[ParticleHandles[6]].Num(), 3);
			EXPECT_TRUE(ClusterMap[ParticleHandles[6]].Contains(ParticleHandles[0]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[6]].Contains(ParticleHandles[1]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[6]].Contains(ParticleHandles[2]));

			EXPECT_TRUE(ParticleHandles[0]->Disabled());
			EXPECT_TRUE(ParticleHandles[1]->Disabled());
			EXPECT_TRUE(ParticleHandles[2]->Disabled());
			EXPECT_TRUE(ParticleHandles[3]->Disabled());
			EXPECT_TRUE(ParticleHandles[4]->Disabled());
			EXPECT_TRUE(ParticleHandles[5]->Disabled());
			EXPECT_FALSE(ParticleHandles[6]->Disabled());
			EXPECT_FALSE(ParticleHandles[7]->Disabled());
			EXPECT_TRUE(ParticleHandles[8]->Disabled());
		}
			
		delete FalloffField;

	}

	TYPED_TEST(AllTraits, GeometryCollection_RigidBodies_Field_ClusterBreak_StrainModel_Test4)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_TwoByTwo_ThreeTransform(FVector(0));

		CreationParameters Params;
		Params.RestCollection = RestCollection;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		Params.Simulating = true;
		Params.EnableClustering = true;
		Params.DamageThreshold = { 1.0 };
		Params.MaxClusterLevel = 1000;		
		Params.ClusterGroupIndex = 0;

		TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSuppliedRestCollection>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

		UnitTest.AddSimulationObject(Collection);
		
		FRadialFalloff* FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 1.5;
		FalloffField->Radius = 100.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		UnitTest.Initialize();
		UnitTest.Advance();

		auto& Clustering = UnitTest.Solver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();

		TArray<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*>& ParticleHandles = Collection->PhysObject->GetSolverParticleHandles();
		{
			FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
			UnitTest.Solver->GetPerSolverField().BufferCommand( { TargetName, FalloffField->NewCopy() });

			EXPECT_TRUE(ParticleHandles[0]->Disabled());
			EXPECT_TRUE(ParticleHandles[1]->Disabled());
			EXPECT_TRUE(ParticleHandles[2]->Disabled());
			EXPECT_TRUE(ParticleHandles[3]->Disabled());
			EXPECT_TRUE(ParticleHandles[4]->Disabled());
			EXPECT_TRUE(ParticleHandles[5]->Disabled());
			EXPECT_FALSE(ParticleHandles[6]->Disabled());

			UnitTest.Advance();

			EXPECT_EQ(ClusterMap.Num(), 2);
			EXPECT_EQ(ClusterMap[ParticleHandles[4]].Num(), 2);
			EXPECT_TRUE(ClusterMap[ParticleHandles[4]].Contains(ParticleHandles[0]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[4]].Contains(ParticleHandles[1]));
			EXPECT_EQ(ClusterMap[ParticleHandles[5]].Num(), 2);
			EXPECT_TRUE(ClusterMap[ParticleHandles[5]].Contains(ParticleHandles[2]));
			EXPECT_TRUE(ClusterMap[ParticleHandles[5]].Contains(ParticleHandles[3]));
			
			EXPECT_TRUE(ParticleHandles[0]->Disabled());
			EXPECT_TRUE(ParticleHandles[1]->Disabled());
			EXPECT_TRUE(ParticleHandles[2]->Disabled());
			EXPECT_TRUE(ParticleHandles[3]->Disabled());
			EXPECT_FALSE(ParticleHandles[4]->Disabled());
			EXPECT_FALSE(ParticleHandles[5]->Disabled());
			EXPECT_TRUE(ParticleHandles[6]->Disabled());
		}
		
		delete FalloffField;
	}

}


