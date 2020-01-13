// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

namespace ChaosTest 
{
	/**
	 * Base class for constraint tests. Provides a basic sim with no builtin constraint support.
	 */
	class FConstraintsTest
	{
	public:

		FConstraintsTest(const int32 NumIterations, const FReal Gravity)
			: Evolution(SOAs, NumIterations)
		{
			PhysicalMaterial = MakeUnique<FChaosPhysicsMaterial>();
			PhysicalMaterial->Friction = 0;
			PhysicalMaterial->Restitution = 0;
			PhysicalMaterial->SleepingLinearThreshold = 0;
			PhysicalMaterial->SleepingAngularThreshold = 0;
			PhysicalMaterial->DisabledLinearThreshold = 0;
			PhysicalMaterial->DisabledAngularThreshold = 0;

			Evolution.GetGravityForces().SetAcceleration(Gravity * FVec3(0, 0, -1));
		}

		virtual ~FConstraintsTest()
		{
		}

		auto AddParticleBox(const FVec3& Position, const FRotation3& Rotation, const FVec3& Size, FReal Mass)
		{
			TGeometryParticleHandle<FReal, 3>& Particle = Mass > SMALL_NUMBER ? *AppendDynamicParticleBox<FReal>(SOAs, Size) : *AppendStaticParticleBox<FReal>(SOAs, Size);

			Particle.X() = Position;
			Particle.R() = Rotation;
			auto PBDParticlePtr = Particle.CastToRigidParticle();
			if(PBDParticlePtr && PBDParticlePtr->ObjectState() == EObjectStateType::Dynamic)
			{
				auto& PBDParticle = *PBDParticlePtr;
				PBDParticle.P() = PBDParticle.X();
				PBDParticle.Q() = PBDParticle.R();
				PBDParticle.M() = PBDParticle.M() * Mass;
				PBDParticle.I() = PBDParticle.I() * Mass;
				PBDParticle.InvM() = PBDParticle.InvM() * ((FReal)1 / Mass);
				PBDParticle.InvI() = PBDParticle.InvI() * ((FReal)1 / Mass);
			}
			Evolution.SetPhysicsMaterial(&Particle, MakeSerializable(PhysicalMaterial));

			return &Particle;
		}

		// Solver state
		TPBDRigidsSOAs<FReal, 3> SOAs;
		TPBDRigidsEvolutionGBF<FReal, 3> Evolution;
		TUniquePtr<FChaosPhysicsMaterial> PhysicalMaterial;

		TGeometryParticleHandle<FReal, 3>* GetParticle(const int32 Idx)
		{
			return SOAs.GetParticleHandles().Handle(Idx).Get();
		}
	};

}