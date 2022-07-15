// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/Evolution/SolverConstraintContainer.h"

namespace Chaos
{
	class FPBDRigidSpringConstraints;
	class FPBDIslandSolverData;

	class CHAOS_API FPBDRigidSpringConstraintHandle final : public TIndexedContainerConstraintHandle<FPBDRigidSpringConstraints>
	{
	public:
		using Base = TIndexedContainerConstraintHandle<FPBDRigidSpringConstraints>;
		using FConstraintContainer = FPBDRigidSpringConstraints;
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FPBDRigidSpringConstraintHandle()
		{
		}
		
		FPBDRigidSpringConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) 
		: TIndexedContainerConstraintHandle<FPBDRigidSpringConstraints>(InConstraintContainer, InConstraintIndex)
		{
		}

		const TVector<FVec3, 2>& GetConstraintPositions() const;
		void SetConstraintPositions(const TVector<FVec3, 2>& ConstraintPositions);
		
		virtual FParticlePair GetConstrainedParticles() const override;

		// Get the rest length of the spring
		FReal GetRestLength() const;
		void SetRestLength(const FReal SpringLength);

		void PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);

		static const FConstraintHandleTypeID& StaticType()
		{
			static FConstraintHandleTypeID STypeID(TEXT("FRigidSpringConstraintHandle"), &FIndexedConstraintHandle::StaticType());
			return STypeID;
		}
	};


	class FPBDRigidSpringConstraints : public FPBDIndexedConstraintContainer
	{
	public:
		// @todo(ccaulfield): an alternative AddConstraint which takes the constrain settings rather than assuming everything is in world-space rest pose

		using Base = FPBDIndexedConstraintContainer;
		using FConstraintContainerHandle = FPBDRigidSpringConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDRigidSpringConstraints>;
		using FConstrainedParticlePair = TVector<TGeometryParticleHandle<FReal, 3>*, 2>;
		using FHandles = TArray<FConstraintContainerHandle*>;
		using FConstraintSolverContainerType = FConstraintSolverContainer;	// @todo(chaos): Add island solver for this constraint type

		FPBDRigidSpringConstraints();
		virtual ~FPBDRigidSpringConstraints();
		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		int32 NumConstraints() const
		{
			return Constraints.Num();
		}

		/**
		 * Add a constraint initialized from current world-space particle positions.
		 * You would use this method when your objects are already positioned in the world.
		 *
		 * \param InConstrainedParticles the two particles connected by the spring
		 * \param InLocations the world-space locations of the spring connectors on each particle
		 */
		FConstraintContainerHandle* AddConstraint(const FConstrainedParticlePair& InConstrainedParticles, const  TVector<FVec3, 2>& InLocations, FReal Stiffness, FReal Damping, FReal RestLength);

		/**
		 * Remove the specified constraint.
		 */
		void RemoveConstraint(int ConstraintIndex);

		/**
		 * Disabled the specified constraint.
		 */
		void DisableConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles) 
		{
			// @todo(chaos)
		}


		//
		// Constraint API
		//
		FHandles& GetConstraintHandles()
		{
			return Handles;
		}
		const FHandles& GetConstConstraintHandles() const
		{
			return Handles;
		}

		const FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex) const
		{
			return Handles[ConstraintIndex];
		}

		FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex)
		{
			return Handles[ConstraintIndex];
		}

		/**
		 * Get the particles that are affected by the specified constraint.
		 */
		const FConstrainedParticlePair& GetConstrainedParticles(int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex];
		}

		/**
		 * Get the local-space constraint positions for each body.
		 */
		const TVector<FVec3, 2>& GetConstraintPositions(int ConstraintIndex) const
		{
			return Distances[ConstraintIndex];
		}

		/**
		 * Set the local-space constraint positions for each body.
		 */
		void SetConstraintPositions(int ConstraintIndex, const TVector<FVec3, 2>& ConstraintPositions)
		{
			Distances[ConstraintIndex] = ConstraintPositions;
		}

		/**
		 * Get the rest length of the spring
		 */
		FReal GetRestLength(int32 ConstraintIndex) const
		{
			return SpringSettings[ConstraintIndex].RestLength;
		}

		/**
		 * Set the rest length of the spring
		 */
		void SetRestLength(int32 ConstraintIndex, const FReal SpringLength)
		{
			SpringSettings[ConstraintIndex].RestLength = SpringLength;
		}

		//
		// Simple Rule API
		//

		void PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
		void ScatterOutput(const FReal Dt, FPBDIslandSolverData& SolverData);

		bool ApplyPhase1(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
		bool ApplyPhase2(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return false; }
		bool ApplyPhase3(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return false; }

		//
		// Island Rule API
		//

		void PrepareTick() {}
		void UnprepareTick() {}
		void UpdatePositionBasedState(const FReal Dt) {}

		void SetNumIslandConstraints(const int32 NumIslandConstraints, FPBDIslandSolverData& SolverData);
		void PreGatherInput(const FReal Dt, const int32 ConstraintIndex, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, const int32 ConstraintIndex, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);

		bool ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
		bool ApplyPhase2Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return false; }
		bool ApplyPhase3Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return false; }

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		void ApplyPhase1Single(const FReal Dt, int32 ConstraintIndex) const;

		void InitDistance(int32 ConstraintIndex, const FVec3& Location0, const FVec3& Location1);

		FVec3 GetDelta(int32 ConstraintIndex, const FVec3& WorldSpaceX1, const FVec3& WorldSpaceX2) const;

		struct FSpringSettings
		{
			FReal Stiffness;
			FReal Damping;
			FReal RestLength;
		};

		TArray<FConstrainedParticlePair> Constraints;
		TArray<TVector<FVec3, 2>> Distances;
		TArray<FSpringSettings> SpringSettings;

		TArray<FSolverBodyPtrPair> ConstraintSolverBodies;

		TArray<FConstraintContainerHandle*> Handles;
		FConstraintHandleAllocator HandleAllocator;
	};
}
