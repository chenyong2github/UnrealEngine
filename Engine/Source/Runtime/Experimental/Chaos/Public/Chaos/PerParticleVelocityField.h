// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/DynamicParticles.h"
#include "Chaos/PerParticleRule.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	// Velocity field basic implementation
	// TODO:
	//  - Turn this into a base class, with inherited uniform and other type of (non-uniform) fields.
	//  - Add drag per particle instead of per field.
	//  - Calculate field effect on the geometry area.
	//  - Expose fluid density (currently using air density).
	template<class T, int d>
	class TPerParticleVelocityField : public TPerParticleRule<T, d>
	{
	public:
		TPerParticleVelocityField()
			: MVelocity(TVector<T, d>((T)0.)), MHalfRhoDragArea((T)0.) {}
		TPerParticleVelocityField(const TVector<T, d>& InVelocity, const T InDrag)
			: MVelocity(InVelocity)
		{
			SetDrag(InDrag);
		}

		virtual ~TPerParticleVelocityField() {}

		// TODO: Remove this - we should no longer be using indices directly.
		//       This has been kept around for cloth, which uses it in
		//       PBDEvolution.
		template<class T_PARTICLES>
		inline void ApplyHelper(T_PARTICLES& InParticles, const T Dt, const int Index) const
		{
			TVector<T, d> Direction = MVelocity - InParticles.V(Index);
			const T RelVelocity = Direction.SafeNormalize();
			InParticles.F(Index) += Direction * MHalfRhoDragArea * FMath::Square(RelVelocity);
		}

		inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int Index) const override //-V762
		{
			ApplyHelper(InParticles, Dt, Index);
		}

		void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const override //-V762
		{
			if (!UsesVelocityField.Contains(Particle.Handle()))
			{
				TVector<T, d> Direction = MVelocity - Particle.V();
				const T RelVelocity = Direction.SafeNormalize();
				Particle.F() += Direction * MHalfRhoDragArea * FMath::Square(RelVelocity);
			}
		}

		void SetEnabled(const TGeometryParticleHandleImp<T, d, true>& Handle, bool bEnabled)
		{
			if (bEnabled)
			{
				if (UsesVelocityField.Contains(&Handle))
				{
					UsesVelocityField.Remove(&Handle);
				}
			}
			else
			{
				if (!UsesVelocityField.Contains(&Handle))
				{
					UsesVelocityField.Add(&Handle);
				}
			}
		}

		void SetVelocity(const TVector<T, d>& InVelocity)
		{
			MVelocity = InVelocity;
		}

		void SetDrag(const T InDrag)
		{
			static const T Area = (T)0.1;  // TODO: Work out a correct calculation of the area
			static const T AirDensity = (T)1.225;  // TODO: Expose fluid density for other fluid effects
			MHalfRhoDragArea = (T)0.5 * AirDensity * InDrag * Area;
		}

	private:
		TSet<const TGeometryParticleHandleImp<T, d, true>*> UsesVelocityField;
		TVector<T, d> MVelocity;
		float MHalfRhoDragArea;
	};
}
