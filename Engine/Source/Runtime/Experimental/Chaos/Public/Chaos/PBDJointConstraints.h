// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Array.h"
#include "Chaos/Core.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

#include "Chaos/ConstraintHandle.h"
#include "Chaos/Joint/PBDJointSolverGaussSeidel.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/PBDJointConstraintTypes.h"

namespace Chaos
{

	class FPBDJointConstraintHandle : public TContainerConstraintHandle<FPBDJointConstraints>
	{
	public:
		using Base = TContainerConstraintHandle<FPBDJointConstraints>;
		using FConstraintContainer = FPBDJointConstraints;

		CHAOS_API FPBDJointConstraintHandle();
		CHAOS_API FPBDJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);

		CHAOS_API void CalculateConstraintSpace(FVec3& OutXa, FMatrix33& OutRa, FVec3& OutXb, FMatrix33& OutRb) const;
		CHAOS_API void SetParticleLevels(const TVector<int32, 2>& ParticleLevels);
		CHAOS_API int32 GetConstraintLevel() const;
		CHAOS_API const FPBDJointSettings& GetSettings() const;
		CHAOS_API TVector<TGeometryParticleHandle<float,3>*, 2> GetConstrainedParticles() const;

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	class CHAOS_API FPBDJointState
	{
	public:
		FPBDJointState();

		// Priorities used for ordering, mass conditioning, projection, and freezing
		int32 Level;
		TVector<int32, 2> ParticleLevels;
	};

	/**
	 * A joint restricting up to 6 degrees of freedom, with linear and angular limits.
	 */
	class CHAOS_API FPBDJointConstraints : public FPBDConstraintContainer
	{
	public:
		using Base = FPBDConstraintContainer;

		using FConstraintContainerHandle = FPBDJointConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDJointConstraints>;
		using FParticlePair = TVector<TGeometryParticleHandle<FReal, 3>*, 2>;
		using FVectorPair = TVector<FVec3, 2>;
		using FTransformPair = TVector<FRigidTransform3, 2>;
		using FHandles = TArray<FConstraintContainerHandle*>;

		FPBDJointConstraints(const FPBDJointSolverSettings& InSettings = FPBDJointSolverSettings());

		virtual ~FPBDJointConstraints();

		const FPBDJointSolverSettings& GetSettings() const;
		void SetSettings(const FPBDJointSolverSettings& InSettings);

		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		int32 NumConstraints() const;

		/**
		 * Add a constraint with particle-space constraint offsets.
		 */
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FRigidTransform3& WorldConstraintFrame);
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames);
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FPBDJointSettings& InConstraintSettings);

		/**
		 * Remove the specified constraint.
		 */
		void RemoveConstraint(int ConstraintIndex);

		// @todo(ccaulfield): rename/remove  this
		void RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles);

		void SetPreApplyCallback(const FJointPostApplyCallback& Callback);
		void ClearPreApplyCallback();

		void SetPostApplyCallback(const FJointPostApplyCallback& Callback);
		void ClearPostApplyCallback();

		void SetPostProjectCallback(const FJointPostApplyCallback& Callback);
		void ClearPostProjectCallback();

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

		const FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex) const;
		FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex);

		/**
		 * Get the particles that are affected by the specified constraint.
		 */
		const FParticlePair& GetConstrainedParticles(int32 ConstraintIndex) const;

		const FPBDJointSettings& GetConstraintSettings(int32 ConstraintIndex) const;

		int32 GetConstraintLevel(int32 ConstraintIndex) const;
		void SetParticleLevels(int32 ConstraintIndex, const TVector<int32, 2>& ParticleLevels);

		//
		// General Rule API
		//

		void UpdatePositionBasedState(const FReal Dt);

		//
		// Simple Rule API
		//

		void Apply(const FReal Dt, const int32 It, const int32 NumIts);
		bool ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts);

		//
		// Island Rule API
		//

		void Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);
		bool ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);


	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		friend class FPBDJointConstraintHandle;

		void PrepareConstraints(FReal Dt);

		void GetConstrainedParticleIndices(const int32 ConstraintIndex, int32& Index0, int32& Index1) const;
		void CalculateConstraintSpace(int32 ConstraintIndex, FVec3& OutX0, FMatrix33& OutR0, FVec3& OutX1, FMatrix33& OutR1) const;
		void UpdateParticleState(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& P, const FRotation3& Q, const bool bUpdateVelocity = true);
		void UpdateParticleState(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& P, const FRotation3& Q, const FVec3& V, const FVec3& W);
		void SortConstraints();

		void SolvePosition_Cholesky(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts);
		void SolvePosition_GaussSiedel(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts);
		void ProjectPosition_GaussSiedel(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts);

		FPBDJointSolverSettings Settings;

		TArray<FPBDJointSettings> ConstraintSettings;
		TArray<FParticlePair> ConstraintParticles;
		TArray<FPBDJointState> ConstraintStates;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;

		FJointPreApplyCallback PreApplyCallback;
		FJointPostApplyCallback PostApplyCallback;
		FJointPostApplyCallback PostProjectCallback;

		// @todo(ccaulfield): optimize storage for joint solver
		TArray<FJointSolverGaussSeidel> ConstraintSolvers;

		bool bRequiresSort;
	};

}
