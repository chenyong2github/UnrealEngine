// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Array.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDRigidSpringConstraintsBase
	{
	public:
		using FConstrainedParticlePair = TVector<TGeometryParticleHandle<T, d>*, 2>;

		TPBDRigidSpringConstraintsBase(const T InStiffness = (T)1)
		    : Stiffness(InStiffness)
		{
		}

		TPBDRigidSpringConstraintsBase(const TArray<TVector<T, d>>& Locations0,  const TArray<TVector<T, d>>& Locations1, TArray<FConstrainedParticlePair>&& InConstraints, const T InStiffness = (T)1)
		    : Constraints(MoveTemp(InConstraints)), Stiffness(InStiffness)
		{
			UpdateDistances(Locations0, Locations1);
		}
	
		virtual ~TPBDRigidSpringConstraintsBase() {}
	
		int32 NumConstraints() const
		{
			return Constraints.Num();
		}

		const FConstrainedParticlePair& ConstraintParticles(int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex];
		}

		void UpdateDistances(const TArray<TVector<T, d>>& Locations0, const TArray<TVector<T, d>>& Locations1);
		TVector<T, d> GetDelta(const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2, const int32 ConstraintIndex) const;
	
		void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
		{
			// @todo(ccaulfield): constraint management
		}

	protected:
		TArray<FConstrainedParticlePair> Constraints;
		TArray<TVector<TVector<T, 3>, 2>> Distances;
		TArray<T> SpringDistances;
	
	private:
		T Stiffness;
	};
}
