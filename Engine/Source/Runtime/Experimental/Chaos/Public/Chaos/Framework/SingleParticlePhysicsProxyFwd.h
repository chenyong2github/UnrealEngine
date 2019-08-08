// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Chaos
{
	template <typename T, int d>
	class TGeometryParticle;

	template <typename T, int d>
	class TKinematicGeometryParticle;

	template <typename T, int d>
	class TPBDRigidParticle;
}

template <typename T>
class CHAOS_API FSingleParticlePhysicsProxy;

typedef FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>> FGeometryParticlePhysicsProxy;
typedef FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> > FKinematicGeometryParticlePhysicsProxy;
typedef FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float, 3> > FRigidParticlePhysicsProxy;