// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	class FPBDRigidSpringConstraints;

	class CHAOS_API FPBDRigidSpringConstraintHandle : public TContainerConstraintHandle<FPBDRigidSpringConstraints>
	{
	public:
		using Base = TContainerConstraintHandle<FPBDRigidSpringConstraints>;
		using FConstraintContainer = FPBDRigidSpringConstraints;
		using FGeometryParticleHandle = TGeometryParticleHandle<FReal, 3>;

		FPBDRigidSpringConstraintHandle()
		{
		}
		
		FPBDRigidSpringConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) 
		: TContainerConstraintHandle<FPBDRigidSpringConstraints>(StaticType(), InConstraintContainer, InConstraintIndex) 
		{
		}

		static FConstraintHandle::EType StaticType() { return FConstraintHandle::EType::RigidSpring; }

		const TVector<FVec3, 2>& GetConstraintPositions() const;
		void SetConstraintPositions(const TVector<FVec3, 2>& ConstraintPositions);
		
		TVector<FGeometryParticleHandle*, 2> GetConstrainedParticles() const;

		// Get the rest length of the spring
		FReal GetRestLength() const;
		void SetRestLength(const FReal SpringLength);

	};


	class FPBDRigidSpringConstraints : public FPBDConstraintContainer
	{
	public:
		// @todo(ccaulfield): an alternative AddConstraint which takes the constrain settings rather than assuming everything is in world-space rest pose

		using Base = FPBDConstraintContainer;
		using FConstraintContainerHandle = FPBDRigidSpringConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDRigidSpringConstraints>;
		using FConstrainedParticlePair = TVector<TGeometryParticleHandle<FReal, 3>*, 2>;
		using FHandles = TArray<FConstraintContainerHandle*>;

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
		// Island Rule API
		//

		void PrepareTick() {}

		void UnprepareTick() {}

		void PrepareIteration(FReal Dt) {}

		void UnprepareIteration(FReal Dt) {}

		void UpdatePositionBasedState(const FReal Dt) {}

		bool Apply(const FReal Dt, const int32 It, const int32 NumIts);

		bool ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts)
		{
			return false;
		}

		bool Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);

		bool ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
		{
			return false;
		}

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		void ApplySingle(const FReal Dt, int32 ConstraintIndex) const;

		void UpdateDistance(int32 ConstraintIndex, const FVec3& Location0, const FVec3& Location1);

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

		TArray<FConstraintContainerHandle*> Handles;
		FConstraintHandleAllocator HandleAllocator;
	};
}
