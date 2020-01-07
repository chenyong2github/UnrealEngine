// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/Utilities.h"

#include "Templates/EnableIf.h"

#include <functional>

namespace Chaos
{
	template<class T, int d>
	class TPBDSpringConstraintsBase
	{
	public:
		TPBDSpringConstraintsBase(const T Stiffness = (T)1)
		    : MStiffness(Stiffness)
		{}
		TPBDSpringConstraintsBase(const TDynamicParticles<T, d>& InParticles, TArray<TVector<int32, 2>>&& Constraints, const T Stiffness = (T)1)
		    : MConstraints(MoveTemp(Constraints)), MStiffness(Stiffness)
		{
			RemoveDuplicateConstraints();
			UpdateDistances(InParticles);
		}
		TPBDSpringConstraintsBase(const TDynamicParticles<T, d>& InParticles, const TArray<TVector<int32, 3>>& Constraints, const T Stiffness = (T)1)
		    : MStiffness(Stiffness)
		{
			Init<3>(Constraints);
			RemoveDuplicateConstraints();
			UpdateDistances(InParticles);
		}
		TPBDSpringConstraintsBase(const TDynamicParticles<T, d>& InParticles, const TArray<TVector<int32, 4>>& Constraints, const T Stiffness = (T)1)
		    : MStiffness(Stiffness)
		{
			Init<4>(Constraints);
			RemoveDuplicateConstraints();
			UpdateDistances(InParticles);
		}
		virtual ~TPBDSpringConstraintsBase()
		{}

		template<int32 Valence>
		typename TEnableIf<Valence == 2, void>::Type
		Init(TArray<TVector<int32, Valence>>&& Constraints)
		{
			MConstraints = MoveTemp(Constraints);
			MDists.Reset();
		}
		template<int32 Valence>
		typename TEnableIf<Valence == 2, void>::Type
		Init(const TArray<TVector<int32, Valence>>& Constraints)
		{
			MConstraints = Constraints;
			MDists.Reset();
		}

		template<int32 Valence>
		typename TEnableIf<Valence != 2, void>::Type
		Init(const TArray<TVector<int32, Valence>>& Constraints)
		{
			MConstraints.Reset();
			MConstraints.Reserve(Constraints.Num() * Chaos::Utilities::NChooseR(Valence, 2));
			MDists.Reset();
			for (auto Constraint : Constraints)
			{
				for (int32 i = 0; i < Valence-1; i++)
				{
					for(int32 j=i+1; j < Valence; j++)
					{
						const int32 i1 = Constraint[i];
						const int32 i2 = Constraint[j];
						MConstraints.Add(TVector<int32, 2>(i1, i2));
					}
				}
			}
		}
		
		uint32 RemoveDuplicateConstraints()
		{
			const uint32 OriginalSize = MConstraints.Num();
			TArray<TVector<int32, 2>> TrimmedConstraints;
			TSet<TVector<int32, 2>>   ConstraintsAlreadyAdded;
			TrimmedConstraints.Reserve(MConstraints.Num());
			ConstraintsAlreadyAdded.Reserve(MConstraints.Num());
			for (TVector<int32, 2> Constraint : MConstraints)
			{
				if (Constraint[0] > Constraint[1])
				{
					Swap(Constraint[0], Constraint[1]);
				}
				if (!ConstraintsAlreadyAdded.Contains(Constraint))
				{
					TrimmedConstraints.Add(Constraint);
					ConstraintsAlreadyAdded.Add(Constraint);
				}
			}
			MConstraints = MoveTemp(TrimmedConstraints);
			return OriginalSize - MConstraints.Num();
		}

		template<class T_PARTICLES>
		void UpdateDistances(const T_PARTICLES& InParticles)
		{
			MDists.Reset();
			MDists.Reserve(MConstraints.Num());
			for (auto Constraint : MConstraints)
			{
				const int32 i1 = Constraint[0];
				const int32 i2 = Constraint[1];
				const TVector<T, d>& P1 = InParticles.X(i1);
				const TVector<T, d>& P2 = InParticles.X(i2);
				MDists.Add((P1 - P2).Size());
			}
		}

		template<class T_PARTICLES>
		inline TVector<T, d> GetDelta(const T_PARTICLES& InParticles, const int32 i) const
		{
			const auto& Constraint = MConstraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];

			if (InParticles.InvM(i2) == 0 && InParticles.InvM(i1) == 0)
				return TVector<T, d>(0);
			const T CombinedMass = InParticles.InvM(i2) + InParticles.InvM(i1);

			const TVector<T, d>& P1 = InParticles.P(i1);
			const TVector<T, d>& P2 = InParticles.P(i2);
			TVector<T, d> Direction = P1 - P2;
			const T Distance = Direction.SafeNormalize();

			const TVector<T, d> Delta = (Distance - MDists[i]) * Direction;
			return MStiffness * Delta / CombinedMass;
		}

		//! Same as \c GetDelta(), but doesn't check for a zero length vector between 
		// the dynamic particle positions prior to normalizing. Use this if you happen
		// to know that the particle positions aren't coincident.
		template<class T_PARTICLES>
		inline TVector<T, d> GetUnsafeDelta(const T_PARTICLES& InParticles, const int32 i) const
		{
			const auto& Constraint = MConstraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];

			if (InParticles.InvM(i2) == 0 && InParticles.InvM(i1) == 0)
				return TVector<T, d>(0);
			const T CombinedMass = InParticles.InvM(i2) + InParticles.InvM(i1);

			const TVector<T, d>& P1 = InParticles.P(i1);
			const TVector<T, d>& P2 = InParticles.P(i2);
			TVector<T, d> Direction = P1 - P2;
			const T Distance = Direction.Normalize();

			const TVector<T, d> Delta = (Distance - MDists[i]) * Direction;
			return MStiffness * Delta / CombinedMass;
		}

	protected:
		TArray<TVector<int32, 2>> MConstraints;

	private:
		TArray<T> MDists;
		T MStiffness;
	};
}
