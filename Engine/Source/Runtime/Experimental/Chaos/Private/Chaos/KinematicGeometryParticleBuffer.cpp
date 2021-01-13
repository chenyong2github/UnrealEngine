// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/KinematicGeometryParticleBuffer.h"
#include "Chaos/PBDRigidParticleBuffer.h"

namespace Chaos
{

void FKinematicGeometryParticleBuffer::SetV(const FVec3& InV, bool bInvalidate)
{
	if (bInvalidate)
	{
		FPBDRigidParticleBuffer* Dyn = FPBDRigidParticleBuffer::Cast(this);
		if (Dyn && Dyn->ObjectState() == EObjectStateType::Sleeping && !InV.IsNearlyZero())
		{
			Dyn->SetObjectState(EObjectStateType::Dynamic, true);
		}
	}
	MVelocities.Modify(bInvalidate, MDirtyFlags, Proxy, [&InV](auto& Data) { Data.SetV(InV); });
}

void FKinematicGeometryParticleBuffer::SetW(const FVec3& InW, bool bInvalidate)
{
	if (bInvalidate)
	{
		FPBDRigidParticleBuffer* Dyn = FPBDRigidParticleBuffer::Cast(this);
		if (Dyn && Dyn->ObjectState() == EObjectStateType::Sleeping && !InW.IsNearlyZero())
		{
			Dyn->SetObjectState(EObjectStateType::Dynamic, true);
		}
	}
	MVelocities.Modify(bInvalidate, MDirtyFlags, Proxy, [&InW](auto& Data) { Data.SetW(InW); });
}

EObjectStateType FKinematicGeometryParticleBuffer::ObjectState() const
{
	const FPBDRigidParticleBuffer* Dyn = FPBDRigidParticleBuffer::Cast(this);
	return Dyn ? Dyn->ObjectState() : EObjectStateType::Kinematic;
}


}
