// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "KinematicGeometryParticleProxy.h"
#include "Chaos/PBDRigidParticleBuffer.h"

namespace Chaos
{

class FPBDRigidParticleProxy : public FKinematicGeometryParticleProxy
{
public:
	FPBDRigidParticleProxy(FPBDRigidParticleBuffer* InParticleBuffer, UObject* InOwner = nullptr);

};

}