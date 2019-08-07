// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDRigidDynamicSpringConstraintsBase2
	{
	public:
		TPBDRigidDynamicSpringConstraintsBase2(const T InStiffness = (T)1)
		    : Stiffness(InStiffness)
		{
		}
		TPBDRigidDynamicSpringConstraintsBase2(TArray<TVector<TGeometryParticleHandle<T, d>*, 2>>&& InConstraints, const T InCreationThreshold = (T)1, const int32 InMaxSprings = 1, const T InStiffness = (T)1)
		    : Constraints(MoveTemp(InConstraints)), CreationThreshold(InCreationThreshold), MaxSprings(InMaxSprings), Stiffness(InStiffness)
		{
			Distances.SetNum(Constraints.Num());
			SpringDistances.SetNum(Constraints.Num());
		}
	
		virtual ~TPBDRigidDynamicSpringConstraintsBase2() {}

		int32 NumConstraints() const
		{
			return Constraints.Num();
		}

		const TVector<TGeometryParticleHandle<T, d>*, 2>& ConstraintParticles(int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex];
		}

		CHAOS_API void UpdatePositionBasedState();

		TVector<T, d> GetDelta(const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2, const int32 ConstraintIndex, const int32 SpringIndex) const;

		void Add(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1)
		{
			Constraints.Add({ Particle0, Particle1 });
			Distances.Add({});
			SpringDistances.Add({});
		}

		void SetDistance(const T Threshold)
		{
			CreationThreshold = Threshold;
		}

		void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
		{
			// @todo(ccaulfield): constraint management
		}

	protected:
		TArray<TVector<TGeometryParticleHandle<T, d>*, 2>> Constraints;
		TArray<TArray<TVector<TVector<T, 3>, 2>>> Distances;
		TArray<TArray<T>> SpringDistances;
	
	private:
		T CreationThreshold;
		int32 MaxSprings;
		T Stiffness;
	};
}
