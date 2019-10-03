// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Array.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/PBDJointConstraintTypes.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDJointConstraintHandle : public TContainerConstraintHandle<TPBDJointConstraints<T, d>>
	{
	public:
		using Base = TContainerConstraintHandle<TPBDJointConstraints<T, d>>;
		using FConstraintContainer = TPBDJointConstraints<T, d>;

		CHAOS_API TPBDJointConstraintHandle();
		CHAOS_API TPBDJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);

		CHAOS_API void CalculateConstraintSpace(TVector<T, d>& OutXa, PMatrix<T, d, d>& OutRa, TVector<T, d>& OutXb, PMatrix<T, d, d>& OutRb, TVector<T, d>& OutCR) const;
		CHAOS_API void SetParticleLevels(const TVector<int32, 2>& ParticleLevels);
		CHAOS_API int32 GetConstraintLevel() const;
		CHAOS_API const TPBDJointSettings<T, d>& GetSettings() const;
	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	template<class T, int d>
	class CHAOS_API TPBDJointState
	{
	public:
		TPBDJointState();

		// Priorities used for ordering, mass conditioning, projection, and freezing
		int32 Level;
		TVector<int32, 2> ParticleLevels;
	};

	/**
	 * A joint restricting up to 6 degrees of freedom, with linear and angular limits.
	 */
	template<class T, int d>
	class TPBDJointConstraints : public TPBDConstraintContainer<T, d>
	{
	public:
		using Base = TPBDConstraintContainer<T, d>;
		using FReal = T;
		static const int Dimensions = d;

		using FConstraintHandle = TPBDJointConstraintHandle<FReal, Dimensions>;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDJointConstraints<FReal, Dimensions>>;
		using FParticlePair = TVector<TGeometryParticleHandle<FReal, Dimensions>*, 2>;
		using FVectorPair = TVector<TVector<FReal, Dimensions>, 2>;
		using FTransformPair = TVector<TRigidTransform<FReal, Dimensions>, 2>;
		using FJointSettings = TPBDJointSettings<FReal, Dimensions>;
		using FJointState = TPBDJointState<FReal, Dimensions>;

		CHAOS_API TPBDJointConstraints(const TPBDJointSolverSettings<T, d>& InSettings = TPBDJointSolverSettings<T, d>());

		CHAOS_API virtual ~TPBDJointConstraints();

		CHAOS_API const TPBDJointSolverSettings<T, d>& GetSettings() const;
		CHAOS_API void SetSettings(const TPBDJointSolverSettings<T, d>& InSettings);

		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		CHAOS_API int32 NumConstraints() const;

		/**
		 * Add a constraint with particle-space constraint offsets.
		 */
		CHAOS_API FConstraintHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const TRigidTransform<FReal, Dimensions>& WorldConstraintFrame);
		CHAOS_API FConstraintHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames);
		CHAOS_API FConstraintHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const TPBDJointSettings<T, d>& InConstraintSettings);

		/**
		 * Remove the specified constraint.
		 */
		CHAOS_API void RemoveConstraint(int ConstraintIndex);

		// @todo(ccaulfield): rename/remove  this
		CHAOS_API void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles);

		CHAOS_API void SetPreApplyCallback(const TJointPostApplyCallback<T, d>& Callback);
		CHAOS_API void ClearPreApplyCallback();

		CHAOS_API void SetPostApplyCallback(const TJointPostApplyCallback<T, d>& Callback);
		CHAOS_API void ClearPostApplyCallback();

		//
		// Constraint API
		//

		CHAOS_API const FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex) const;
		CHAOS_API FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex);

		/**
		 * Get the particles that are affected by the specified constraint.
		 */
		CHAOS_API const FParticlePair& GetConstrainedParticles(int32 ConstraintIndex) const;

		CHAOS_API const TPBDJointSettings<T, d>& GetConstraintSettings(int32 ConstraintIndex) const;

		CHAOS_API int32 GetConstraintLevel(int32 ConstraintIndex) const;
		CHAOS_API void SetParticleLevels(int32 ConstraintIndex, const TVector<int32, 2>& ParticleLevels);

		//
		// Island Rule API
		//

		CHAOS_API void UpdatePositionBasedState(const T Dt);

		CHAOS_API void Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);

		// @todo(ccaulfield): remove  this
		CHAOS_API void ApplyPushOut(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles);

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		friend class TPBDJointConstraintHandle<T, d>;

		CHAOS_API void CalculateConstraintSpace(int32 ConstraintIndex, TVector<T, d>& OutX0, PMatrix<T, d, d>& OutR0, TVector<T, d>& OutX1, PMatrix<T, d, d>& OutR1, TVector<T, d>& OutAngles) const;

		CHAOS_API void ApplySingle(const T Dt, const int32 ConstraintIndex, const int32 It, const int32 NumIts);

		TPBDJointSolverSettings<T, d> Settings;

		TArray<FJointSettings> ConstraintSettings;
		TArray<FParticlePair> ConstraintParticles;
		TArray<FJointState> ConstraintStates;

		TArray<FConstraintHandle*> Handles;
		FConstraintHandleAllocator HandleAllocator;

		TJointPreApplyCallback<T, d> PreApplyCallback;
		TJointPostApplyCallback<T, d> PostApplyCallback;
	};

}
