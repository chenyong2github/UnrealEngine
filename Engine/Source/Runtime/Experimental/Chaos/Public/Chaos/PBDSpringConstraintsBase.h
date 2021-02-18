// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Utilities.h"
#include "Chaos/RigidParticles.h"
#include "Chaos/DynamicParticles.h"

#include "Templates/EnableIf.h"

#include <functional>

namespace Chaos
{
	class FPBDSpringConstraintsBase
	{
	public:
		FPBDSpringConstraintsBase(const FReal Stiffness = (FReal)1.)
		    : MStiffness(Stiffness)
		{}
		FPBDSpringConstraintsBase(const FDynamicParticles& InParticles, TArray<TVec2<int32>>&& Constraints, const FReal Stiffness = (FReal)1., bool bStripKinematicConstraints = false)
		    : MConstraints(MoveTemp(Constraints)), MStiffness(Stiffness)
		{
			RemoveRedundantConstraints(InParticles, bStripKinematicConstraints);
			UpdateDistances(InParticles);
		}
		FPBDSpringConstraintsBase(const TRigidParticles<FReal, 3>& InParticles, TArray<TVec2<int32>>&& Constraints, const FReal Stiffness = (FReal)1., bool bStripKinematicConstraints = false)
		    : MConstraints(MoveTemp(Constraints)), MStiffness(Stiffness)
		{
			RemoveRedundantConstraints(InParticles, bStripKinematicConstraints);
			UpdateDistances(InParticles);
		}
		FPBDSpringConstraintsBase(const FDynamicParticles& InParticles, const TArray<TVec3<int32>>& Constraints, const FReal Stiffness = (FReal)1., bool bStripKinematicConstraints = false)
		    : MStiffness(Stiffness)
		{
			Init<3>(Constraints);
			RemoveRedundantConstraints(InParticles, bStripKinematicConstraints);
			UpdateDistances(InParticles);
		}
		FPBDSpringConstraintsBase(const FDynamicParticles& InParticles, const TArray<TVector<int32, 4>>& Constraints, const FReal Stiffness = (FReal)1., bool bStripKinematicConstraints = false)
		    : MStiffness(Stiffness)
		{
			Init<4>(Constraints);
			RemoveRedundantConstraints(InParticles, bStripKinematicConstraints);
			UpdateDistances(InParticles);
		}
		virtual ~FPBDSpringConstraintsBase()
		{}

		void SetStiffness(FReal InStiffness) { MStiffness = FMath::Clamp(InStiffness, (FReal)0., (FReal)1.); }

	protected:
		template<class T_PARTICLES>
		inline FVec3 GetDelta(const T_PARTICLES& InParticles, const int32 i) const
		{
			const auto& Constraint = MConstraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];

			if (InParticles.InvM(i2) == 0 && InParticles.InvM(i1) == 0)
				return FVec3(0);
			const FReal CombinedMass = InParticles.InvM(i2) + InParticles.InvM(i1);

			const FVec3& P1 = InParticles.P(i1);
			const FVec3& P2 = InParticles.P(i2);
			FVec3 Direction = P1 - P2;
			const FReal Distance = Direction.SafeNormalize();

			const FVec3 Delta = (Distance - MDists[i]) * Direction;
			return MStiffness * Delta / CombinedMass;
		}

		//! Same as \c GetDelta(), but doesn't check for a zero length vector between 
		// the dynamic particle positions prior to normalizing. Use this if you happen
		// to know that the particle positions aren't coincident.
		template<class T_PARTICLES>
		inline FVec3 GetUnsafeDelta(const T_PARTICLES& InParticles, const int32 i) const
		{
			const auto& Constraint = MConstraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];

			if (InParticles.InvM(i2) == 0 && InParticles.InvM(i1) == 0)
				return FVec3(0);
			const FReal CombinedMass = InParticles.InvM(i2) + InParticles.InvM(i1);

			const FVec3& P1 = InParticles.P(i1);
			const FVec3& P2 = InParticles.P(i2);
			FVec3 Direction = P1 - P2;
			const FReal Distance = Direction.Normalize();

			const FVec3 Delta = (Distance - MDists[i]) * Direction;
			return MStiffness * Delta / CombinedMass;
		}

	private:
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
						MConstraints.Add(TVec2<int32>(i1, i2));
					}
				}
			}
		}
		
		template<class T_PARTICLES>
		uint32 RemoveRedundantConstraints(const T_PARTICLES& InParticles, bool bStripKinematicConstraints)
		{
			const uint32 OriginalSize = MConstraints.Num();
			TArray<TVec2<int32>> TrimmedConstraints;
			TSet<TVec2<int32>>   ConstraintsAlreadyAdded;
			TrimmedConstraints.Reserve(MConstraints.Num());
			ConstraintsAlreadyAdded.Reserve(MConstraints.Num());
			for (TVec2<int32> Constraint : MConstraints)
			{
				if (Constraint[0] > Constraint[1])
				{
					Swap(Constraint[0], Constraint[1]);
				}
				if (!ConstraintsAlreadyAdded.Contains(Constraint))
				{
					ConstraintsAlreadyAdded.Add(Constraint);

					if (!bStripKinematicConstraints || InParticles.InvM(Constraint[0]) > 0 || InParticles.InvM(Constraint[1]) > 0)  // Only add constraint to kinematic/dynamic or dynamic/dynamic particles
					{
						TrimmedConstraints.Add(Constraint);
					}
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
				const FVec3& P1 = InParticles.X(i1);
				const FVec3& P2 = InParticles.X(i2);
				MDists.Add((P1 - P2).Size());
			}
		}

	protected:
		TArray<TVec2<int32>> MConstraints;
		TArray<FReal> MDists;
		FReal MStiffness;
	};
}
