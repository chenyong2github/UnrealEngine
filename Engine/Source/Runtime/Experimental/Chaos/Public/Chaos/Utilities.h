// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/DynamicParticles.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/Vector.h"

#include "Math/NumericLimits.h"
#include "Templates/IsIntegral.h"

namespace Chaos
{
	namespace Utilities
	{
		template<class T_PARTICLES, class T_PARTICLES_BASE, class T, int d>
		inline TFunction<void(T_PARTICLES&, const T, const int32)> GetGravityFunction(const TVector<T, d>& Direction, const T Magnitude)
		{
			return [Gravity = PerParticleGravity<T, d>(Direction, Magnitude)](T_PARTICLES_BASE& InParticles, const T Dt, const int Index) {
				Gravity.Apply(InParticles, Dt, Index);
			};
		}
		template<class T, int d>
		inline TFunction<void(TPBDParticles<T, d>&, const T, const int32)> GetDeformablesGravityFunction(const TVector<T, d>& Direction, const T Magnitude)
		{
			return GetGravityFunction<TPBDParticles<T, d>, TDynamicParticles<T, d>, T, d>(Direction, Magnitude);
		}
		template<class T, int d>
		inline auto GetRigidsGravityFunction(const TVector<T, d>& Direction, const T Magnitude)
		{
			return[Gravity = PerParticleGravity<T, d>(Direction, Magnitude)](TTransientPBDRigidParticleHandle<T,d>& Particle, const T Dt) {
				Gravity.Apply(Particle, Dt);
			};
		}

		// Take the factorial of \p Num, which should be of integral type.
		template<class TINT = uint64>
		TINT Factorial(TINT Num)
		{
			static_assert(TIsIntegral<TINT>::Value, "Templated type must be integral.");
			TINT Result = Num;
			while (Num > 2)
			{
				Result *= --Num;
			}
			return Result;
		}

		// Number of ways to choose of \p R elements from a set of size \p N, with no repetitions.
		template<class TINT = uint64>
		TINT NChooseR(const TINT N, const TINT R)
		{
			static_assert(TIsIntegral<TINT>::Value, "Templated type must be integral.");
			return Factorial(N) / (Factorial(R) * Factorial(N - R));
		}

		// Number of ways to choose of \p R elements from a set of size \p N, with repetitions.
		template<class TINT = uint64>
		TINT NPermuteR(const TINT N, const TINT R)
		{
			static_assert(TIsIntegral<TINT>::Value, "Templated type must be integral.");
			return Factorial(N) / Factorial(N - R);
		}

		template<class T>
		void GetMinAvgMax(const TArray<T>& Values, T& MinV, double& AvgV, T& MaxV)
		{
			MinV = TNumericLimits<T>::Max();
			MaxV = TNumericLimits<T>::Lowest();
			AvgV = 0.0;
			for (const T& V : Values)
			{
				MinV = V < MinV ? V : MinV;
				MaxV = V > MaxV ? V : MaxV;
				AvgV += V;
			}
			if (Values.Num())
			{
				AvgV /= Values.Num();
			}
			else
			{
				MinV = MaxV = 0;
			}
		}

	} // namespace Utilities
} // namespace Chaos