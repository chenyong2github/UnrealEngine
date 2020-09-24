// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HeadlessChaosTestConstraints.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/Box.h"
#include "Chaos/Utilities.h"

namespace ChaosTest
{
	using namespace Chaos;

	/**
	 * Base class for simple joint chain tests.
	 */
	template <typename TEvolution>
	class FJointChainTest : public FConstraintsTest<TEvolution>
	{
	public:
		using Base = FConstraintsTest<TEvolution>;
		using Base::Evolution;
		using Base::AddParticleBox;
		using Base::GetParticle;

		FJointChainTest(const int32 NumIterations, const FReal Gravity)
			: Base(NumIterations, Gravity)
			, JointsRule(Joints)
		{
			Evolution.AddConstraintRule(&JointsRule);
		}

		FPBDJointConstraintHandle* AddJoint(const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& InConstrainedParticleIndices, const int32 JointIndex)
		{
			FPBDJointConstraintHandle* Joint = Joints.AddConstraint(InConstrainedParticleIndices, FRigidTransform3(JointPositions[JointIndex], FRotation3::FromIdentity()));

			if (JointIndex < JointSettings.Num())
			{
				Joint->SetSettings(JointSettings[JointIndex]);
			}

			return Joint;
		}

		void Create()
		{
			for (int32 ParticleIndex = 0; ParticleIndex < ParticlePositions.Num(); ++ParticleIndex)
			{
				AddParticleBox(ParticlePositions[ParticleIndex], FRotation3::MakeFromEuler(FVec3(0.f, 0.f, 0.f)).GetNormalized(), ParticleSizes[ParticleIndex], ParticleMasses[ParticleIndex]);
			}

			for (int32 JointIndex = 0; JointIndex < JointPositions.Num(); ++JointIndex)
			{
				const TVector<TGeometryParticleHandle<FReal, 3>*, 2> ConstraintedParticleIds(GetParticle(JointParticleIndices[JointIndex][0]), GetParticle(JointParticleIndices[JointIndex][1]));
				AddJoint(ConstraintedParticleIds, JointIndex);
			}
		}

		// Create a pendulum chain along the specified direction with the first particle kinematic
		void InitChain(int32 NumParticles, const FVec3& Dir)
		{
			FReal Size = 10.0f;
			FReal Separation = 30.0f;

			for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
			{
				FReal D = ParticleIndex * Separation;
				FReal M = (ParticleIndex == 0) ? 0.0f : 100.0f;
				ParticlePositions.Add(D * Dir);
				ParticleSizes.Add(FVec3(Size));
				ParticleMasses.Add(M);
			}

			for (int32 JointIndex = 0; JointIndex < NumParticles - 1; ++JointIndex)
			{
				int32 ParticleIndex0 = JointIndex;
				int32 ParticleIndex1 = JointIndex + 1;
				FReal D = JointIndex * Separation;
				JointPositions.Add(D * Dir);
				JointParticleIndices.Add(TVector<int32, 2>(ParticleIndex0, ParticleIndex1));
			}

			JointSettings.SetNum(NumParticles - 1);
		}

		// Initial particles setup
		TArray<FVec3> ParticlePositions;
		TArray<FVec3> ParticleSizes;
		TArray<FReal> ParticleMasses;

		// Initial joints setup
		TArray<FVec3> JointPositions;
		TArray<TVector<int32, 2>> JointParticleIndices;
		TArray<FPBDJointSettings> JointSettings;

		// Solver state
		FPBDJointConstraints Joints;
		TPBDConstraintIslandRule<FPBDJointConstraints> JointsRule;
	};

}