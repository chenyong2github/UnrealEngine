// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestCollisions.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosCollisionConstraints.h"
#include "Chaos/GJK.h"
#include "Chaos/Pair.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDCollisionConstraintsPGS.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"
#include "Modules/ModuleManager.h"

#define SMALL_THRESHOLD 1e-4

#define RESET_PQ(Particle) Particle->P() = Particle->X(); Particle->Q() = Particle->R()
#define INVARIANT_XR_START(Particle) FVec3 InvariantPreX_##Particle = Particle->X(); FRotation3 InvariantPreR_##Particle = Particle->R()
#define INVARIANT_XR_END(Particle) EXPECT_TRUE(InvariantPreX_##Particle.Equals(Particle->X())); EXPECT_TRUE(InvariantPreR_##Particle.Equals(Particle->R()))
#define INVARIANT_VW_START(Particle) FVec3 InvariantPreV_##Particle = Particle->V(); FVec3 InvariantPreW_##Particle = Particle->W()
#define INVARIANT_VW_END(Particle) EXPECT_TRUE(InvariantPreV_##Particle.Equals(Particle->V())); EXPECT_TRUE(InvariantPreW_##Particle.Equals(Particle->W()))

namespace ChaosTest {

	using namespace Chaos;

	DEFINE_LOG_CATEGORY_STATIC(LogHChaosCollisions, Verbose, All);

	void LevelsetConstraint()
	{
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;
		
		FPBDRigidsSOAs Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto Box1 = AppendDynamicParticleBox(Particles);
		Box1->X() = FVec3(1.f);
		Box1->R() = FRotation3(FQuat::Identity);
		Box1->P() = Box1->X();
		Box1->Q() = Box1->R();
		Box1->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		auto Box2 = AppendDynamicParticleBox(Particles);
		Box2->X() = FVec3(1.5f, 1.5f, 1.9f);
		Box2->R() = FRotation3(FQuat::Identity);
		Box2->P() = Box2->X();
		Box2->Q() = Box2->R();
		Box2->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		FPBDCollisionConstraintAccessor Collisions(Particles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, 1, 1);
		Collisions.ComputeConstraints(0.f);
		EXPECT_EQ(Collisions.NumConstraints(), 1);

		FCollisionConstraintBase& Constraint = Collisions.GetConstraint(0);
		if (auto PBDRigid = Constraint.Particle[0]->CastToRigidParticle())
		{
			//Question: non dynamics don't have collision particles, seems wrong if the levelset is dynamic and the static is something like a box
			PBDRigid->CollisionParticles()->UpdateAccelerationStructures();
		}
		Collisions.UpdateLevelsetConstraint(*Constraint.template As<FPBDCollisionConstraints::FPointContactConstraint>());

		EXPECT_EQ(Constraint.Particle[0], Box2);
		EXPECT_EQ(Constraint.Particle[1], Box1);
		EXPECT_TRUE(Constraint.GetNormal().operator==(FVec3(0, 0, 1)));
		EXPECT_TRUE(FMath::Abs(ChaosTest::SignedDistance(*Constraint.Particle[0], Constraint.GetLocation())) < SMALL_THRESHOLD);
	}

	void LevelsetConstraintGJK()
	{
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;

		FPBDRigidsSOAs Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto Box1 = AppendDynamicParticleConvexBox(Particles, FVec3(1.f) );
		Box1->X() = FVec3(0.f);
		Box1->R() = FRotation3(FQuat::Identity);
		Box1->P() = Box1->X();
		Box1->Q() = Box1->R();
		Box1->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		auto Box2 = AppendDynamicParticleBox(Particles, FVec3(1.f) );
		Box2->X() = FVec3(1.25f, 0.f, 0.f);
		Box2->R() = FRotation3(FQuat::Identity);
		Box2->P() = Box2->X();
		Box2->Q() = Box2->R();
		Box2->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		FPBDCollisionConstraintAccessor Collisions(Particles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, 1, 1);
		Collisions.ComputeConstraints(0.f);
		EXPECT_EQ(Collisions.NumConstraints(), 1);

		FCollisionConstraintBase & Constraint = Collisions.GetConstraint(0);
		Collisions.UpdateLevelsetConstraint(*Constraint.template As<FPBDCollisionConstraints::FPointContactConstraint>());
		
		EXPECT_EQ(Constraint.Particle[0], Box2);
		EXPECT_EQ(Constraint.Particle[1], Box1);
		EXPECT_TRUE(Constraint.GetNormal().operator==(FVec3(0, 0, 1)));
		EXPECT_TRUE(FMath::Abs(ChaosTest::SignedDistance(*Constraint.Particle[0], Constraint.GetLocation())) < SMALL_THRESHOLD);
	}
	
	void CollisionBoxPlane()
	{
		// test a box and plane in a colliding state
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)1;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;

		FPBDRigidsSOAs Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto Floor = AppendStaticAnalyticFloor(Particles);
		auto Box = AppendDynamicParticleBox(Particles);
		Box->X() = FVec3(0, 1, 0);
		Box->R() = FRotation3(FQuat::Identity);
		Box->V() = FVec3(0, 0, -1);
		Box->PreV() = Box->V();
		Box->P() = Box->X();
		Box->Q() = Box->R();
		Box->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		const FReal Dt = 1 / 24.;

		FPBDCollisionConstraintAccessor Collisions(Particles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, 2, 5);

		Collisions.ComputeConstraints(Dt);
		EXPECT_EQ(Collisions.NumConstraints(), 1);

		FCollisionConstraintBase & Constraint = Collisions.GetConstraint(0);
		if (auto PBDRigid = Constraint.Particle[0]->CastToRigidParticle())
		{
			PBDRigid->CollisionParticles()->UpdateAccelerationStructures();
		}
		Collisions.UpdateLevelsetConstraint(*Constraint.template As<FPBDCollisionConstraints::FPointContactConstraint>());

		EXPECT_EQ(Constraint.Particle[0], Box);
		EXPECT_EQ(Constraint.Particle[1], Floor);
		EXPECT_TRUE(Constraint.GetNormal().operator==(FVec3(0, 0, 1)));
		EXPECT_TRUE(FMath::Abs(ChaosTest::SignedDistance(*Constraint.Particle[0], Constraint.GetLocation())) < SMALL_THRESHOLD);
		EXPECT_TRUE(FMath::Abs(Constraint.GetPhi() - FReal(-0.5) ) < SMALL_THRESHOLD );

		{
			INVARIANT_XR_START(Box);
			const int32 NumIts = 1;
			for (int32 It = 0; It < NumIts; ++It)
			{
				Collisions.Apply(Dt, { Collisions.GetConstraintHandle(0) }, It, NumIts);
			}
			INVARIANT_XR_END(Box);
		}

		// Velocity is below the restitution threshold, so expecting 0 velocity despite the fact that restitution is 1
		EXPECT_TRUE(Box->V().Equals(FVec3(0)));
		EXPECT_TRUE(Box->W().Equals(FVec3(0)));

		{
			RESET_PQ(Box);
			{
				INVARIANT_XR_START(Box);
				INVARIANT_VW_START(Box);
				const int32 NumIts = 10;
				for (int32 It = 0; It < NumIts; ++It)
				{
					Collisions.ApplyPushOut(Dt, { Collisions.GetConstraintHandle(0) }, TSet<const FGeometryParticleHandle*>(), It, NumIts);
				}
				INVARIANT_XR_END(Box);
				INVARIANT_VW_END(Box);
			}
		}
		EXPECT_NEAR(Box->P().Z, 0.5f, 1.e-2f);
	}

	void CollisionConvexConvex()
	{

		// test a box and plane in a colliding state
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;

		FPBDRigidsSOAs Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto Floor = AppendStaticConvexFloor(Particles);
		auto Box = AppendDynamicParticleConvexBox( Particles, FVec3(50) );
		Box->X() = FVec3(0, 0, 49);
		Box->R() = FRotation3(FQuat::Identity);
		Box->V() = FVec3(0, 0, -1);
		Box->PreV() = Box->V();
		Box->P() = Box->X();
		Box->Q() = Box->R();
		Box->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		const FReal Dt = 1 / 24.;

		FPBDCollisionConstraintAccessor Collisions(Particles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, 2, 5);

		Collisions.ComputeConstraints(Dt);
		EXPECT_EQ(Collisions.NumConstraints(), 1);

		FRigidBodyPointContactConstraint * Constraint = Collisions.GetConstraint(0).template As<FRigidBodyPointContactConstraint>();
		EXPECT_TRUE(Constraint != nullptr);

		Collisions.Update(*Constraint);

		EXPECT_EQ(Constraint->Particle[0], Box);
		EXPECT_EQ(Constraint->Particle[1], Floor);
		EXPECT_TRUE(Constraint->GetNormal().operator==(FVec3(0, 0, 1)));
		EXPECT_TRUE(FMath::Abs( Constraint->GetLocation().Z - FVec3(0,0,-1).Z ) < SMALL_THRESHOLD);
		EXPECT_TRUE(FMath::Abs(Constraint->GetPhi() - FReal(-1.0)) < SMALL_THRESHOLD);

		{
			INVARIANT_XR_START(Box);
			Collisions.Apply(Dt, { Collisions.GetConstraintHandle(0) }, 0, 1);
			INVARIANT_XR_END(Box);
		}

		// 0 restitution so expecting 0 velocity
		//EXPECT_TRUE(Box->V().Equals(FVec3(0)));
		//EXPECT_TRUE(Box->W().Equals(FVec3(0)));

		{
			RESET_PQ(Box);
			{
				INVARIANT_XR_START(Box);
				INVARIANT_VW_START(Box);
				const int32 NumIts = 10;
				for (int32 It = 0; It < NumIts; ++It)
				{
					Collisions.ApplyPushOut(Dt, { Collisions.GetConstraintHandle(0) }, TSet<const FGeometryParticleHandle*>(), It, NumIts);
				}
				INVARIANT_XR_END(Box);
				INVARIANT_VW_END(Box);
			}
		}

		//EXPECT_TRUE(Box->P().Equals(FVector(0.f, 0.f, 50.f)));
		//EXPECT_TRUE(Box->Q().Equals(FQuat::Identity));

	}

	void CollisionBoxPlaneZeroResitution()
	{
		// test a box and plane in a colliding state
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;
		
		FPBDRigidsSOAs Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto Floor = AppendStaticAnalyticFloor(Particles);
		auto Box = AppendDynamicParticleBox(Particles);
		Box->X() = FVec3(0, 1, 0);
		Box->R() = FRotation3(FQuat::Identity);
		Box->V() = FVec3(0, 0, -1);
		Box->PreV() = Box->V();
		Box->P() = Box->X();
		Box->Q() = Box->R();
		Box->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		const FReal Dt = 1 / 24.;

		FPBDCollisionConstraintAccessor Collisions(Particles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, 2, 5);

		Collisions.ComputeConstraints(Dt);
		EXPECT_EQ(Collisions.NumConstraints(), 1);

		FCollisionConstraintBase & Constraint = Collisions.GetConstraint(0);
		if (auto PBDRigid = Constraint.Particle[0]->CastToRigidParticle())
		{
			PBDRigid->CollisionParticles()->UpdateAccelerationStructures();
		}
		Collisions.UpdateLevelsetConstraint(*Constraint.template As<FPBDCollisionConstraints::FPointContactConstraint>());

		EXPECT_EQ(Constraint.Particle[0], Box);
		EXPECT_EQ(Constraint.Particle[1], Floor);
		EXPECT_TRUE(Constraint.GetNormal().operator==(FVec3(0, 0, 1)));
		EXPECT_TRUE(FMath::Abs(ChaosTest::SignedDistance(*Constraint.Particle[0], Constraint.GetLocation())) < SMALL_THRESHOLD);
		EXPECT_TRUE(FMath::Abs(Constraint.GetPhi() - FReal(-0.5)) < SMALL_THRESHOLD);

		{
			INVARIANT_XR_START(Box);
			Collisions.Apply(Dt, { Collisions.GetConstraintHandle(0) }, 0, 1);
			INVARIANT_XR_END(Box);
		}

		// 0 restitution so expecting 0 velocity
		EXPECT_TRUE(Box->V().Equals(FVec3(0)));
		EXPECT_TRUE(Box->W().Equals(FVec3(0)));

		{
			RESET_PQ(Box);
			{
				INVARIANT_XR_START(Box);
				INVARIANT_VW_START(Box);
				const int32 NumIts = 10;
				for (int32 It = 0; It < NumIts; ++It)
				{
					Collisions.ApplyPushOut(Dt, { Collisions.GetConstraintHandle(0) }, TSet<const FGeometryParticleHandle*>(), It, NumIts);
				}
				INVARIANT_XR_END(Box);
				INVARIANT_VW_END(Box);
			}
		}

		EXPECT_TRUE(FVec3::IsNearlyEqual(Box->P(), FVector(0.f, 1.f, 0.5f), 1.e-2f));
	}

	void CollisionBoxPlaneRestitution()
	{
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)1;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;
		FPBDRigidsSOAs Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto Floor = AppendStaticAnalyticFloor(Particles);
		auto Box = AppendDynamicParticleBox(Particles);
		Box->X() = FVec3(0, 0, 0);
		Box->R() = FRotation3(FQuat::Identity);
		Box->V() = FVec3(0, 0, -100);
		Box->PreV() = Box->V();
		Box->P() = Box->X();
		Box->Q() = Box->R();
		Box->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		const FReal Dt = 1 / 24.;

		FPBDCollisionConstraintAccessor Collisions(Particles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, 2, 5);

		Collisions.ComputeConstraints(Dt);
		EXPECT_EQ(Collisions.NumConstraints(), 1);

		FCollisionConstraintBase & Constraint = Collisions.GetConstraint(0);
		if (auto PBDRigid = Constraint.Particle[0]->CastToRigidParticle())
		{
			PBDRigid->CollisionParticles()->UpdateAccelerationStructures();
		}
		Collisions.UpdateLevelsetConstraint(*Constraint.template As<FPBDCollisionConstraints::FPointContactConstraint>());
		EXPECT_EQ(Constraint.Particle[0], Box);
		EXPECT_EQ(Constraint.Particle[1], Floor);
		EXPECT_TRUE(Constraint.GetNormal().operator==(FVec3(0, 0, 1)));
		EXPECT_TRUE(FMath::Abs(ChaosTest::SignedDistance(*Constraint.Particle[0], Constraint.GetLocation())) < SMALL_THRESHOLD);
		EXPECT_TRUE(FMath::Abs(Constraint.GetPhi() - FReal(-0.5)) < SMALL_THRESHOLD);

		{
			INVARIANT_XR_START(Box);
			Collisions.Apply(Dt, { Collisions.GetConstraintHandle(0) }, 0, 1);
			INVARIANT_XR_END(Box);
		}

		// full restitution, so expecting negative velocity
		EXPECT_TRUE(Box->V().Equals(FVec3(0.f, 0.f, 100.f)));
		EXPECT_TRUE(Box->W().Equals(FVec3(0)));
		// collision occurs before full dt takes place, so need some bounce back for the remaining time we have
		//EXPECT_TRUE(Particles.P(BoxId).Equals(Particles.X(BoxId)));
		//EXPECT_TRUE(Particles.Q(BoxId).Equals(Particles.R(BoxId)));

		{
			RESET_PQ(Box);
			{
				INVARIANT_XR_START(Box);
				const int32 NumIts = 10;
				for (int32 It = 0; It < NumIts; ++It)
				{
					Collisions.ApplyPushOut(Dt, { Collisions.GetConstraintHandle(0) }, TSet<const FGeometryParticleHandle*>(), It, NumIts);
				}
				INVARIANT_XR_END(Box);
			}
		}

		//for push out velocity is unimportant, so expecting simple pop out
		EXPECT_TRUE(FVec3::IsNearlyEqual(Box->P(), FVector(0.f, 0.f, 0.5f), 1.e-2f));
		EXPECT_TRUE(Box->Q().Equals(FQuat::Identity));
	}

	// This test will make sure that a dynamic cube colliding with a static floor will have the correct bounce velocity
	// for a restitution of 0.5
	// The dynamic cube will collide with one of its vertices onto a face of the static cube
	void CollisionCubeCubeRestitution()
	{
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)0.5;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;
		FPBDRigidsSOAs Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		FGeometryParticleHandle* StaticCube = AppendStaticParticleBox(Particles, FVec3(100.0f));
		StaticCube->X() = FVec3(0, 0, -50.0f);
		FPBDRigidParticleHandle* DynamicCube = AppendDynamicParticleBox(Particles, FVec3(100.0f));
		DynamicCube->X() = FVec3(0, 0, 80); // Penetrating by about 5cm
		DynamicCube->R() = FRotation3::FromElements( 0.27059805f, 0.27059805f, 0.0f, 0.923879532f ); // Rotate so that vertex collide
		DynamicCube->V() = FVec3(0, 0, -100);
		DynamicCube->PreV() = DynamicCube->V();
		DynamicCube->P() = DynamicCube->X();
		DynamicCube->Q() = DynamicCube->R();
		DynamicCube->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		const FReal Dt = 1 / 24.;

		FPBDCollisionConstraintAccessor Collisions(Particles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, 2, 5);

		Collisions.ComputeConstraints(Dt);
		EXPECT_EQ(Collisions.NumConstraints(), 1);

		if (Collisions.NumConstraints() <= 0)
		{
			return;
		}

		FCollisionConstraintBase& Constraint = Collisions.GetConstraint(0);
		if (FPBDRigidParticleHandle* PBDRigid = Constraint.Particle[0]->CastToRigidParticle())
		{
			PBDRigid->CollisionParticles()->UpdateAccelerationStructures();
		}
		Collisions.UpdateLevelsetConstraint(*Constraint.template As<FPBDCollisionConstraints::FPointContactConstraint>());
		EXPECT_EQ(Constraint.Particle[0], DynamicCube);
		EXPECT_EQ(Constraint.Particle[1], StaticCube);
		EXPECT_TRUE(Constraint.GetNormal().operator==(FVec3(0, 0, 1)));
		EXPECT_TRUE(FMath::Abs(ChaosTest::SignedDistance(*Constraint.Particle[0], Constraint.GetLocation())) < SMALL_THRESHOLD);
		{
			INVARIANT_XR_START(DynamicCube);
			Collisions.Apply(Dt, { Collisions.GetConstraintHandle(0) }, 0, 1);
			INVARIANT_XR_END(DynamicCube);
		}

		// This test's tolerances are set to be very crude as to not be over sensitive (for now)
		EXPECT_TRUE(DynamicCube->V().Z > 10.0f);  // restitution not too low
		EXPECT_TRUE(DynamicCube->V().Z < 70.0f);  // restitution not too high
		EXPECT_TRUE(FMath::Abs(DynamicCube->V().X) < 1.0f);
		EXPECT_TRUE(FMath::Abs(DynamicCube->V().Y) < 1.0f);
	}

	void CollisionBoxToStaticBox()
	{
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;
		FPBDRigidsSOAs Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto StaticBox = AppendStaticParticleBox(Particles);
		StaticBox->X() = FVec3(-0.05f, -0.05f, -0.1f);
		StaticBox->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		auto Box2 = AppendDynamicParticleBox(Particles);
		FVec3 StartingPoint(0.5f);
		Box2->X() = StartingPoint;
		Box2->P() = Box2->X();
		Box2->Q() = Box2->R();
		Box2->V() = FVec3(0, 0, -1);
		Box2->PreV() = Box2->V();
		Box2->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		FBox Region(FVector(.2), FVector(.5));

		FReal Dt = 1 / 24.;

		FPBDCollisionConstraintAccessor Collisions(Particles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, 1, 1);
		Collisions.ComputeConstraints(Dt);
		EXPECT_EQ(Collisions.NumConstraints(), 1);

		FCollisionConstraintBase & Constraint = Collisions.GetConstraint(0);
		Collisions.Update(Constraint);

		//Collisions.UpdateLevelsetConstraintGJK(Particles, Constraint);
		//EXPECT_EQ(Constraint.ParticleIndex, 1);
		//EXPECT_EQ(Constraint.LevelsetIndex, 0);
		//EXPECT_TRUE(Constraint.GetNormal().Equals(FVector(0.0, 1.0, 0.0f))); // GJK returns a different result!
		//EXPECT_TRUE(FMath::Abs(ChaosTest::SignedDistance(Particles, Constraint.ParticleIndex, Constraint.GetLocation())) < SMALL_THRESHOLD);
		//EXPECT_TRUE(FMath::Abs(Constraint.GetPhi() - T(-0.233)) < SMALL_THRESHOLD);

		if (auto PBDRigid = Constraint.Particle[0]->CastToRigidParticle())
		{
			PBDRigid->CollisionParticles()->UpdateAccelerationStructures();
		}

		EXPECT_EQ(Constraint.Particle[0], Box2);
		EXPECT_EQ(Constraint.Particle[1], StaticBox);
		EXPECT_TRUE(Constraint.GetNormal().Equals(FVector(0.0, 0.0, 1.0f)));
		EXPECT_TRUE(FMath::Abs(ChaosTest::SignedDistance(*Constraint.Particle[0], Constraint.GetLocation())) < SMALL_THRESHOLD);
		EXPECT_TRUE(FMath::Abs(Constraint.GetPhi() - FReal(-0.4)) < SMALL_THRESHOLD);


		EXPECT_TRUE(FMath::Abs(Box2->V().Size() - 1.f)<SMALL_THRESHOLD ); // no velocity change yet  

		{
			INVARIANT_XR_START(Box2);
			INVARIANT_XR_START(StaticBox);
			Collisions.Apply(Dt, { Collisions.GetConstraintHandle(0) }, 0, 1);
			INVARIANT_XR_END(Box2);
			INVARIANT_XR_END(StaticBox);
		}

		EXPECT_TRUE(Box2->V().Size()<FVector(0,-1,0).Size()); // slowed down  
		EXPECT_TRUE(Box2->W().Size()>0); // now has rotation 

		RESET_PQ(Box2);
		{
			//INVARIANT_XR_START(Box2);
			//INVARIANT_XR_START(StaticBox);
			//INVARIANT_VW_START(Box2);
			const int32 NumIts = 10;
			for (int32 It = 0; It < NumIts; ++It)
			{
				Collisions.ApplyPushOut(Dt, { Collisions.GetConstraintHandle(0) }, TSet<const FGeometryParticleHandle*>(), It, NumIts);
			}
			//INVARIANT_XR_END(Box2);
			//INVARIANT_XR_END(StaticBox);
			//INVARIANT_VW_END(Box2);
		}

		EXPECT_FALSE(Box2->P().Equals(StartingPoint)); // moved
		EXPECT_FALSE(Box2->Q().Equals(FQuat::Identity)); // and rotated
	}

	void CollisionPGS()
	{
#if CHAOS_PARTICLEHANDLE_TODO
		TSet<int32> ActiveIndices;
		TArray<TSet<int32>> IslandParticles;
		TArray<int32> IslandSleepCounts;
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;
		Chaos::FPBDRigidParticles Particles;
		Particles.AddArray(&Collided);
		Particles.AddArray(&PhysicsMaterials);

		int32 BoxId1 = AppendParticleBox<FReal>(Particles);
		Particles.X(BoxId1) = FVec3(0.f, 0.f, 0.5f);
		Particles.R(BoxId1) = FRotation3(FQuat::Identity);
		Particles.V(BoxId1) = FVec3(0.f, 0.f, -10.f);
		PhysicsMaterials[BoxId1]  = MakeSerializable(PhysicsMaterial);

		int32 BoxId2 = AppendParticleBox<FReal>(Particles);
		Particles.X(BoxId2) = FVec3(0.f, 0.f, 0.5f);
		Particles.R(BoxId2) = FRotation3(FQuat::Identity);
		Particles.V(BoxId2) = FVec3(0.f, 0.f, -10.f);
		PhysicsMaterials[BoxId2]  = MakeSerializable(PhysicsMaterial);

		int32 FloorId = AppendStaticAnalyticFloor<FReal>(Particles);

		ActiveIndices.Add(BoxId1);
		ActiveIndices.Add(BoxId2);
		ActiveIndices.Add(FloorId);

		TArray<int32> Indices = ActiveIndices.Array();

		FPBDCollisionConstraintPGS CollisionConstraints(Particles, Indices, Collided, PhysicsMaterials);
		FRigidBodyContactConstraintPGS Constraint1;
		Constraint1.ParticleIndex = BoxId1;
		Constraint1.LevelsetIndex = BoxId2;
		FRigidBodyContactConstraintPGS Constraint2;
		Constraint2.ParticleIndex = BoxId1;
		Constraint2.LevelsetIndex = FloorId;
		FRigidBodyContactConstraintPGS Constraint3;
		Constraint3.ParticleIndex = BoxId2;
		Constraint3.LevelsetIndex = FloorId;
		CollisionConstraints.Constraints.Add(Constraint1);
		CollisionConstraints.Constraints.Add(Constraint2);
		CollisionConstraints.Constraints.Add(Constraint3);
		CollisionConstraints.Apply(Particles, 1.f, { 0, 1, 2 });
		EXPECT_LT(FMath::Abs(Particles.V(BoxId1)[2]), 1e-3);
		EXPECT_LT(FMath::Abs(Particles.V(BoxId2)[2]), 1e-3);
#endif
	}

	void CollisionPGS2()
	{
#if CHAOS_PARTICLEHANDLE_TODO
		TSet<int32> ActiveIndices;
		TArray<TSet<int32>> IslandParticles;
		TArray<int32> IslandSleepCounts;
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = (FReal)0;
		PhysicsMaterial->Restitution = (FReal)0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;
		Chaos::FPBDRigidParticles Particles;
		Particles.AddArray(&Collided);
		Particles.AddArray(&PhysicsMaterials);

		int32 BoxId1 = AppendParticleBox<FReal>(Particles);
		Particles.X(BoxId1) = FVec3(0.f, 0.f, 0.5f);
		Particles.R(BoxId1) = FRotation3(FQuat::Identity);
		Particles.V(BoxId1) = FVec3(0.f, 0.f, -10.f);
		PhysicsMaterials[BoxId1] = MakeSerializable(PhysicsMaterial);

		int32 BoxId2 = AppendParticleBox<FReal>(Particles);
		Particles.X(BoxId2) = FVec3(0.f, 0.f, 0.5f);
		Particles.R(BoxId2) = FRotation3(FQuat::Identity);
		Particles.V(BoxId2) = FVec3(0.f, 0.f, -10.f);
		PhysicsMaterials[BoxId2] = MakeSerializable(PhysicsMaterial);

		int32 FloorId = AppendStaticAnalyticFloor<FReal>(Particles);

		ActiveIndices.Add(BoxId1);
		ActiveIndices.Add(BoxId2);
		ActiveIndices.Add(FloorId);

		TArray<int32> Indices = ActiveIndices.Array();

		FPBDCollisionConstraintPGS CollisionConstraints(Particles, Indices, Collided, PhysicsMaterials);
		FRigidBodyContactConstraintPGS Constraint1;
		Constraint1.ParticleIndex = BoxId1;
		Constraint1.LevelsetIndex = BoxId2;
		FRigidBodyContactConstraintPGS Constraint2;
		Constraint2.ParticleIndex = BoxId1;
		Constraint2.LevelsetIndex = FloorId;
		FRigidBodyContactConstraintPGS Constraint3;
		Constraint3.ParticleIndex = BoxId2;
		Constraint3.LevelsetIndex = FloorId;
		CollisionConstraints.Constraints.Add(Constraint2);
		CollisionConstraints.Constraints.Add(Constraint3);
		CollisionConstraints.Constraints.Add(Constraint1);
		CollisionConstraints.Apply(Particles, 1.f, { 0, 1, 2 });
		EXPECT_LT(FMath::Abs(Particles.V(BoxId1)[2] - 0.5), 1e-3);
		EXPECT_LT(FMath::Abs(Particles.V(BoxId2)[2] + 0.5), 1e-3);
#endif
	}
}

