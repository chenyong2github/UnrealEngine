// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ParticleHandle.h"

#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/Framework/PhysicsProxyBase.h"

namespace Chaos
{

	template class CHAOS_API TGeometryParticleData<float, 3>;
	template class CHAOS_API TGeometryParticle<float, 3>;

	template class CHAOS_API TKinematicGeometryParticleData<float, 3>;
	template class CHAOS_API TKinematicGeometryParticle<float, 3>;

	template class CHAOS_API TPBDRigidParticleData<float, 3>;
	template class CHAOS_API TPBDRigidParticle<float, 3>;

	template <>
	void Chaos::TGeometryParticle<float, 3>::MarkDirty(const EParticleFlags DirtyBits, bool bInvalidate )
	{
		if (bInvalidate)
		{
			this->MDirtyFlags.MarkDirty(DirtyBits);

			if (Proxy)
			{
				if (FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
				{
					PhysicsSolverBase->AddDirtyProxy(Proxy);
				}
			}
		}
	}

	template <>
	template <>
	int32 TGeometryParticleHandleImp<float, 3, true>::GetPayload<int32>(int32 Idx)
	{
		return Idx;
	}

	template <>
	template <>
	int32 TGeometryParticleHandleImp<float, 3, false>::GetPayload<int32>(int32 Idx)
	{
		return Idx;
	}

	/*
	template <>
	void TPBDRigidParticle<float, 3>::SetObjectState(const EObjectStateType InState, bool bAllowEvents)
	{
		//todo: look at physics thread logic
		if (bAllowEvents && MObjectState != EObjectStateType::Dynamic && InState == EObjectStateType::Dynamic)
		{
			MAwakeEvent |= true;
		}
		MObjectState = InState;
		this->MarkDirty(EParticleFlags::ObjectState);
	}
	*/

}
