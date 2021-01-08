// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryParticleProxy.h"
#include "Chaos/KinematicGeometryParticleBuffer.h"

namespace Chaos
{

class FKinematicGeometryParticleProxy : public FGeometryParticleProxy
{
public:
	FKinematicGeometryParticleProxy(FKinematicGeometryParticleBuffer* InParticleBuffer, UObject* InOwner = nullptr);

};

}