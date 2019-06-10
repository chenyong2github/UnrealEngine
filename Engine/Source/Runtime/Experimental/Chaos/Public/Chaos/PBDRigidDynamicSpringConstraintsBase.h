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
	class TPBDRigidDynamicSpringConstraintsBase
	{
	  public:
		TPBDRigidDynamicSpringConstraintsBase(const T InStiffness = (T)1)
		    : Stiffness(InStiffness)
		{
		}
		TPBDRigidDynamicSpringConstraintsBase(TArray<TVector<int32, 2>>&& InConstraints, const T InCreationThreshold = (T)1, const int32 InMaxSprings = 1, const T InStiffness = (T)1)
		    : Constraints(MoveTemp(InConstraints)), CreationThreshold(InCreationThreshold), MaxSprings(InMaxSprings), Stiffness(InStiffness)
		{
			Distances.SetNum(Constraints.Num());
			SpringDistances.SetNum(Constraints.Num());
		}
	
		virtual ~TPBDRigidDynamicSpringConstraintsBase() {}

		int32 NumConstraints() const
		{
			return Constraints.Num();
		}

		TVector<int32, 2> ConstraintParticleIndices(int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex];
		}

		CHAOS_API void UpdatePositionBasedState(const TPBDRigidParticles<T, d>& InParticles);
		TVector<T, d> GetDelta(const TPBDRigidParticles<T, d>& InParticles, const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2, const int32 i, const int32 j) const;

		void Add(const int32 Index1, const int32 Index2)
		{
			Constraints.Add(TVector<int32, 2>(Index1, Index2));
			Distances.Add(TArray<TVector<TVector<T, 3>, 2>>());
			SpringDistances.Add(TArray<T>());
		}
		void SetDistance(const T Threshold)
		{
			CreationThreshold = Threshold;
		}

		void RemoveConstraints(const TSet<uint32>& RemovedParticles)
		{
			// @todo(ccaulfield): constraint management
		}

	  protected:
		TArray<TVector<int32, 2>> Constraints;
		TArray<TArray<TVector<TVector<T, 3>, 2>>> Distances;
		TArray<TArray<T>> SpringDistances;
	
	  private:
		T CreationThreshold;
		int32 MaxSprings;
		T Stiffness;
	};
}
