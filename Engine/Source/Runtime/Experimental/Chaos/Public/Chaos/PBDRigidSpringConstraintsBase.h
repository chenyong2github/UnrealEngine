// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PerParticleRule.h"

#include <functional>

namespace Chaos
{
	template<class T, int d>
	class TPBDRigidSpringConstraintsBase
	{
	  public:
		TPBDRigidSpringConstraintsBase(const T InStiffness = (T)1)
		    : Stiffness(InStiffness)
		{
		}
		TPBDRigidSpringConstraintsBase(const TRigidParticles<T, d>& InParticles, const TArray<TVector<T, d>>& Locations0,  const TArray<TVector<T, d>>& Locations1, TArray<TVector<int32, 2>>&& InConstraints, const T InStiffness = (T)1)
		    : Constraints(MoveTemp(InConstraints)), Stiffness(InStiffness)
		{
			UpdateDistances(InParticles, Locations0, Locations1);
		}
	
		virtual ~TPBDRigidSpringConstraintsBase() {}
	
		int32 NumConstraints() const
		{
			return Constraints.Num();
		}

		TVector<int32, 2> ConstraintParticleIndices(int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex];
		}

		void UpdateDistances(const TRigidParticles<T, d>& InParticles, const TArray<TVector<T, d>>& Locations0, const TArray<TVector<T, d>>& Locations1);
		TVector<T, d> GetDelta(const TPBDRigidParticles<T, d>& InParticles, const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2, const int32 i) const;
	
		void RemoveConstraints(const TSet<uint32>& RemovedParticles)
		{
			// @todo(ccaulfield): constraint management
		}

	  protected:
		TArray<TVector<int32, 2>> Constraints;
		TArray<TVector<TVector<T, 3>, 2>> Distances;
		TArray<T> SpringDistances;
	
	  private:
		T Stiffness;
	};
}
