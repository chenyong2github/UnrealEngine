// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	template<class T, int d>
	class TPBD6DJointConstraints;

	template<class T, int d>
	class CHAOS_API TPBD6DJointConstraintHandle : public TContainerConstraintHandle<TPBD6DJointConstraints<T, d>>
	{
	public:
		using Base = TContainerConstraintHandle<TPBD6DJointConstraints<T, d>>;
		using FConstraintContainer = TPBD6DJointConstraints<T, d>;

		TPBD6DJointConstraintHandle() {}
		TPBD6DJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : TContainerConstraintHandle<TPBD6DJointConstraints<T, d>>(InConstraintContainer, InConstraintIndex) {}

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};


	enum class E6DJointMotionType
	{
		Free,
		Limited,
		Locked,
	};


	template<class T, int d>
	class CHAOS_API TPBD6DJointConstraintSettings
	{
	public:
		using FTransformPair = TVector<TRigidTransform<T, d>, 2>;

		TPBD6DJointConstraintSettings();

	private:
		friend class TPBD6DJointConstraints<T, d>;

		// Particle-relative joint axes and positions
		// X Axis: Twist
		// Y Axis: Swing 1
		// Z Axis: Swing 2
		FTransformPair ConstraintFrames;

		// Motion Types
		TVector<E6DJointMotionType, d> LinearMotionTypes;
		TVector<E6DJointMotionType, d> AngularMotionTypes;

		// Motion Limits (if MotionType == Limited)
		TVector<T, d> LinearLimits;
		TVector<T, d> AngularLimits;
	};


	/**
	 * A joint restricting up to 6 degrees of freedom, with linear and angular limits.
	 */
	template<class T, int d>
	class CHAOS_API TPBD6DJointConstraints : public TPBDConstraintContainer<T, d>
	{
	public:
		using Base = TPBDConstraintContainer<T, d>;
		using FReal = T;
		static const int Dimensions = d;
		using FConstraintHandle = TPBD6DJointConstraintHandle<FReal, Dimensions>;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBD6DJointConstraints<FReal, Dimensions>>;
		using FParticlePair = TVector<TGeometryParticleHandle<T, d>*, 2>;
		using FVectorPair = TVector<TVector<T, d>, 2>;
		using FTransformPair = TVector<TRigidTransform<T, d>, 2>;

		TPBD6DJointConstraints(const T InStiffness = (T)1)
			: Stiffness(InStiffness)
		{
		}

		virtual ~TPBD6DJointConstraints() {}

		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		int32 NumConstraints() const
		{
			return ConstraintParticles.Num();
		}

		/** 
		 * Add a constraint with particle-space constraint offsets. 
		 */
		FConstraintHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames)
		{
			int ConstraintIndex = Handles.Num();
			Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
			ConstraintParticles.Add(InConstrainedParticles);
			ConstraintSettings.Add(TPBD6DJointConstraintSettings<T, d>());
			InitConstraint(ConstraintIndex, ConstraintFrames);
			return Handles.Last();
		}

		/**
		 * Remove the specified constraint.
		 */
		void RemoveConstraint(int ConstraintIndex)
		{
			FConstraintHandle* ConstraintHandle = Handles[ConstraintIndex];
			if (ConstraintHandle != nullptr)
			{
				// Release the handle for the freed constraint
				HandleAllocator.FreeHandle(ConstraintHandle);
				Handles[ConstraintIndex] = nullptr;
			}

			// Swap the last constraint into the gap to keep the array packed
			ConstraintParticles.RemoveAtSwap(ConstraintIndex);
			ConstraintSettings.RemoveAtSwap(ConstraintIndex);
			Handles.RemoveAtSwap(ConstraintIndex);

			// Update the handle for the constraint that was moved
			if (ConstraintIndex < Handles.Num())
			{
				SetConstraintIndex(Handles[ConstraintIndex], ConstraintIndex);
			}
		}

		// @todo(ccaulfield): rename/remove  this
		void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
		{
		}

		//
		// Constraint API
		//

		const FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex) const
		{
			return Handles[ConstraintIndex];
		}

		FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex)
		{
			return Handles[ConstraintIndex];
		}

		/**
		 * Get the particles that are affected by the specified constraint.
		 */
		const FParticlePair& GetConstrainedParticles(int32 ConstraintIndex) const
		{
			return ConstraintParticles[ConstraintIndex];
		}


		//
		// Island Rule API
		//

		void UpdatePositionBasedState(const T Dt)
		{
		}

		void Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles)
		{
			for (FConstraintHandle* ConstraintHandle : InConstraintHandles)
			{
				ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
			}
		}

		// @todo(ccaulfield): remove  this
		void ApplyPushOut(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles)
		{
		}


	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

		void ApplySingle(const T Dt, const int32 ConstraintIndex);

	private:
		void InitConstraint(int ConstraintIndex, const FTransformPair& InConstraintTransforms);

		// Double dynamic body solve
		void ApplyDynamicDynamic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 PBDRigid1Index, const bool bApplyProjection);
		TVector<T, d> GetDeltaDynamicDynamic(const TVector<T, d>& P0, const TVector<T, d>& P1, const TVector<T, d>& WorldSpaceX0, const TVector<T, d>& WorldSpaceX1, const PMatrix<T, d, d>& WorldSpaceInvI0, const PMatrix<T, d, d>& WorldSpaceInvI1, const T InvM0, const T InvM1);

		// Single dynamic body solve
		void ApplyDynamicStatic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 Static1Index, const bool bApplyProjection);
		TVector<T, d> GetDeltaDynamicKinematic(const TVector<T, d>& P0, const TVector<T, d>& WorldSpaceX0, const TVector<T, d>& WorldSpaceX1, const PMatrix<T, d, d>& WorldSpaceInvI0, const T InvM0);

		TArray<TPBD6DJointConstraintSettings<T, d>> ConstraintSettings;
		TArray<FParticlePair> ConstraintParticles;
		T Stiffness;

		TArray<FConstraintHandle*> Handles;
		FConstraintHandleAllocator HandleAllocator;
	};
}
