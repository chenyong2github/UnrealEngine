// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ParticleHandle.h"
#include "Chaos/PerParticleRule.h"

namespace Chaos
{
	template<class T, int d>
	class TPerParticleExternalForces : public TPerParticleRule<T, d>
	{
	public:
		TPerParticleExternalForces() {}

		virtual ~TPerParticleExternalForces() {}

		inline void Apply(TTransientPBDRigidParticleHandle<T, d>& HandleIn, const T Dt) const override //-V762
		{
			if (TPBDRigidParticleHandleImp<T, d, true>* Handle = HandleIn.Handle())
			{
				Handle->F() += Handle->ExternalForce();
				Handle->Torque() += Handle->ExternalTorque();
			}
		}
	};
}
