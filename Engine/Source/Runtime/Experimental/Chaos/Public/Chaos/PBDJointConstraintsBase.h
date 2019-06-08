// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PerParticleRule.h"

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

		TPBDJointConstraintsBase(const TRigidParticles<T, d>& InParticles, const TArray<TVector<T, d>>& Locations, TArray<TVector<int32, 2>>&& InConstraints, const T InStiffness = (T)1)
		    : Constraints(MoveTemp(InConstraints)), Stiffness(InStiffness)
		{
			UpdateDistances(InParticles, Locations);
		}
		virtual ~TPBDJointConstraintsBase() {}
	
		// @todo(ccaulfield): optimize and generalize the constraint allocation/free api. We still want packed arrays, so we need handles not direct indices.
		int32 AddConstraint(const TRigidParticles<T, d>& InParticles, const TVector<int32, 2>& InConstrainedParticleIndices, const TVector<T, d>& InLocation)
		{
			int32 ConstraintIndex = Constraints.Emplace(InConstrainedParticleIndices);
			UpdateDistance(InParticles, InLocation, ConstraintIndex);
			return ConstraintIndex;
		}


	protected:
		void UpdateDistance(const TRigidParticles<T, d>& InParticles, const TVector<T, d>& InLocation, const int32 InConstraintIndex);
		void UpdateDistances(const TRigidParticles<T, d>& InParticles, const TArray<TVector<T, d>>& InLocations);
		TVector<T, d> GetDelta(const TPBDRigidParticles<T, d>& InParticles, const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2, const PMatrix<T, d, d>& WorldSpaceInvI1, const PMatrix<T, d, d>& WorldSpaceInvI2, const int32 i) const;

		TArray<TVector<int32, 2>> Constraints;
		TArray<TVector<TVector<T, 3>, 2>> Distances;
	
	private:
		void UpdateDistanceInternal(const TRigidParticles<T, d>& InParticles, const TVector<T, d>& InLocation, const int32 InConstraintIndex);

		T Stiffness;
	};
}
