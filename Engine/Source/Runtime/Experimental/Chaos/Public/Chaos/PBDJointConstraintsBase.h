// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PerParticleRule.h"
#include "ParticleHandle.h"

namespace Chaos
{
	template<class T, int d>
	class CHAOS_API TPBDJointConstraintsBase
	{
	public:
		TPBDJointConstraintsBase(const T InStiffness = (T)1)
		    : Stiffness(InStiffness)
		{
		}

		TPBDJointConstraintsBase(const TArray<TVector<T, d>>& Locations, TArray<TVector<TGeometryParticleHandle<T,d>*, 2>>&& InConstraints, const T InStiffness = (T)1)
		    : Constraints(MoveTemp(InConstraints)), Stiffness(InStiffness)
		{
			UpdateDistances(Locations);
		}
		virtual ~TPBDJointConstraintsBase() {}
	
		int32 NumConstraints() const
		{
			return Constraints.Num();
		}

		// @todo(ccaulfield): optimize and generalize the constraint allocation/free api. We still want packed arrays, so we need handles not direct indices.
		int32 AddConstraint(const TVector<TGeometryParticleHandle<T,d>*, 2>& InConstrainedParticles, const TVector<T, d>& InLocation)
		{
			int32 ConstraintIndex = Constraints.Emplace(InConstrainedParticles);
			UpdateDistance(InLocation, ConstraintIndex);
			return ConstraintIndex;
		}

		int32 AddConstraintLocal(const TVector<TGeometryParticleHandle<T, d>*, 2>& InConstrainedParticles, const TVector<TVector<T, 3>, 2>& InLocations)
		{
			int32 ConstraintIndex = Constraints.Emplace(InConstrainedParticles);
			Distances.SetNum(Constraints.Num());
			Distances[ConstraintIndex] = InLocations;
			return ConstraintIndex;
		}

		void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
		{
			// @todo(ccaulfield): constraint management
		}

		TArray<TVector<TGeometryParticleHandle<T, d>*, 2>>& GetConstraints()
		{
			return Constraints;
		}

		TVector<TGeometryParticleHandle<T, d>*, 2> ConstraintParticles(int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex];
		}

	protected:
		void ApplySingle(const T Dt, const int32 ConstraintIndex) const
		{
			// @todo(ccaulfield): bApplyProjection should be an option. Either per-constraint or per-container...
			const bool bApplyProjection = true;

			const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = Constraints[ConstraintIndex];
			if (Constraint[0]->AsDynamic() && Constraint[1]->AsDynamic())
			{
				ApplyDynamicDynamic(Dt, ConstraintIndex, 0, 1, bApplyProjection);
			}
			else if (Constraint[0]->AsDynamic())
			{
				ApplyDynamicStatic(Dt, ConstraintIndex, 0, 1, bApplyProjection);
			}
			else
			{
				ApplyDynamicStatic(Dt, ConstraintIndex, 1, 0, bApplyProjection);
			}
		}

	private:
		void UpdateDistanceInternal(const TVector<T, d>& InLocation, const int32 InConstraintIndex);
		void UpdateDistance(const TVector<T, d>& InLocation, const int32 InConstraintIndex);
		void UpdateDistances(const TArray<TVector<T, d>>& InLocations);

		// Double dynamic body solve
		void ApplyDynamicDynamic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 PBDRigid1Index, const bool bApplyProjection) const;
		TVector<T, d> GetDeltaDynamicDynamic(const TVector<T, d>& P0, const TVector<T, d>& P1, const TVector<T, d>& WorldSpaceX0, const TVector<T, d>& WorldSpaceX1, const PMatrix<T, d, d>& WorldSpaceInvI0, const PMatrix<T, d, d>& WorldSpaceInvI1, const T InvM0, const T InvM1) const;

		// Single dynamic body solve
		void ApplyDynamicStatic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 Static1Index, const bool bApplyProjection) const;
		TVector<T, d> GetDeltaDynamicKinematic(const TVector<T, d>& P0, const TVector<T, d>& WorldSpaceX0, const TVector<T, d>& WorldSpaceX1, const PMatrix<T, d, d>& WorldSpaceInvI0, const T InvM0) const;

		// @todo(ccaulfield): we never iterate over these separately. Do we still want SoA?
		TArray<TVector<TGeometryParticleHandle<T, d>*, 2>> Constraints;
		TArray<TVector<TVector<T, 3>, 2>> Distances;
		T Stiffness;
	};
}
