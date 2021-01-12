// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "KinematicGeometryParticleBuffer.h"

namespace Chaos
{

class FPBDRigidParticleBuffer : public FKinematicGeometryParticleBuffer
{
	using FGeometryParticleBuffer::MDirtyFlags;
	using FGeometryParticleBuffer::Proxy;

public:
	FPBDRigidParticleBuffer(const TPBDRigidParticleParameters<FReal, 3>& DynamicParams = TPBDRigidParticleParameters<FReal, 3>())
		: FKinematicGeometryParticleBuffer(DynamicParams), MWakeEvent(EWakeEventEntry::None)
	{
		this->Type = EParticleType::Rigid;
		MIsland = INDEX_NONE;
		MToBeRemovedOnFracture = false;
		PBDRigidParticleDefaultConstruct<FReal, 3>(*this, DynamicParams);
		ClearForces();
		ClearTorques();
		SetObjectState(DynamicParams.bStartSleeping ? EObjectStateType::Sleeping : EObjectStateType::Dynamic);
		ClearEvents();
		SetInitialized(false);
	}

	static const FPBDRigidParticleBuffer* Cast(const FGeometryParticleBuffer* Buffer)
	{
		return Buffer && Buffer->ObjectType() >= EParticleType::Rigid ? static_cast<const FPBDRigidParticleBuffer*>(Buffer) : nullptr;
	}

	static FPBDRigidParticleBuffer* Cast(FGeometryParticleBuffer* Buffer)
	{
		return Buffer && Buffer->ObjectType() >= EParticleType::Rigid ? static_cast<FPBDRigidParticleBuffer*>(Buffer) : nullptr;
	}

	int32 CollisionGroup() const { return MMiscData.Read().CollisionGroup(); }
	void SetCollisionGroup(const int32 InCollisionGroup)
	{
		MMiscData.Modify(true, MDirtyFlags, Proxy, [InCollisionGroup](auto& Data) { Data.SetCollisionGroup(InCollisionGroup); });
	}

	bool GravityEnabled() const { return MMiscData.Read().GravityEnabled(); }
	void SetGravityEnabled(const bool InGravityEnabled)
	{
		MMiscData.Modify(true, MDirtyFlags, Proxy, [InGravityEnabled](auto& Data) { Data.SetGravityEnabled(InGravityEnabled); });
	}

	bool OneWayInteraction() const { return MMiscData.Read().OneWayInteraction(); }
	void SetOneWayInteraction(const bool InOneWayInteraction)
	{
		MMiscData.Modify(true, MDirtyFlags, Proxy, [InOneWayInteraction](auto& Data) { Data.SetOneWayInteraction(InOneWayInteraction); });
	}

	//todo: remove this
	bool IsInitialized() const { return MInitialized; }
	void SetInitialized(const bool InInitialized)
	{
		this->MInitialized = InInitialized;
	}

	void SetResimType(EResimType ResimType)
	{
		MMiscData.Modify(true, MDirtyFlags, Proxy, [ResimType](auto& Data) { Data.SetResimType(ResimType); });
	}

	EResimType ResimType() const
	{
		return MMiscData.Read().ResimType();
	}

	const FVec3& F() const { return MDynamics.Read().F(); }
	void AddForce(const FVec3& InF, bool bInvalidate = true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate, MDirtyFlags, Proxy, [&InF](auto& Data) { Data.SetF(InF + Data.F()); });
	}

	void ClearForces(bool bInvalidate = true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate, MDirtyFlags, Proxy, [](auto& Data) { Data.SetF(FVec3(0)); });
	}

	void ApplyDynamicsWeight(const FReal DynamicsWeight)
	{
		if (MDynamics.IsDirty(MDirtyFlags))
		{
			MDynamics.Modify(false, MDirtyFlags, Proxy, [DynamicsWeight](auto& Data)
				{
					Data.SetF(Data.F() * DynamicsWeight);
					Data.SetTorque(Data.Torque() * DynamicsWeight);
				});
		}
	}

	const FVec3& Torque() const { return MDynamics.Read().Torque(); }
	void AddTorque(const FVec3& InTorque, bool bInvalidate = true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate, MDirtyFlags, Proxy, [&InTorque](auto& Data) { Data.SetTorque(InTorque + Data.Torque()); });
	}

	void ClearTorques(bool bInvalidate = true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate, MDirtyFlags, Proxy, [](auto& Data) { Data.SetTorque(FVec3(0)); });
	}

	const FVec3& LinearImpulse() const { return MDynamics.Read().LinearImpulse(); }
	void SetLinearImpulse(const FVec3& InLinearImpulse, bool bInvalidate = true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate, MDirtyFlags, Proxy, [&InLinearImpulse](auto& Data) { Data.SetLinearImpulse(InLinearImpulse); });
	}

	const FVec3& AngularImpulse() const { return MDynamics.Read().AngularImpulse(); }
	void SetAngularImpulse(const FVec3& InAngularImpulse, bool bInvalidate = true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate, MDirtyFlags, Proxy, [&InAngularImpulse](auto& Data) { Data.SetAngularImpulse(InAngularImpulse); });
	}

	void SetDynamics(const FParticleDynamics& InDynamics, bool bInvalidate = true)
	{
		MDynamics.Write(InDynamics, bInvalidate, MDirtyFlags, Proxy);
	}

	const PMatrix<FReal, 3, 3>& I() const { return MMassProps.Read().I(); }
	void SetI(const PMatrix<FReal, 3, 3>& InI)
	{
		MMassProps.Modify(true, MDirtyFlags, Proxy, [&InI](auto& Data) { Data.SetI(InI); });
	}

	const PMatrix<FReal, 3, 3>& InvI() const { return MMassProps.Read().InvI(); }
	void SetInvI(const PMatrix<FReal, 3, 3>& InInvI)
	{
		MMassProps.Modify(true, MDirtyFlags, Proxy, [&InInvI](auto& Data) { Data.SetInvI(InInvI); });
	}

	FReal M() const { return MMassProps.Read().M(); }
	void SetM(const FReal& InM)
	{
		MMassProps.Modify(true, MDirtyFlags, Proxy, [InM](auto& Data) { Data.SetM(InM); });
	}

	FReal InvM() const { return MMassProps.Read().InvM(); }
	void SetInvM(const FReal& InInvM)
	{
		MMassProps.Modify(true, MDirtyFlags, Proxy, [InInvM](auto& Data) { Data.SetInvM(InInvM); });
	}

	const FVec3& CenterOfMass() const { return MMassProps.Read().CenterOfMass(); }
	void SetCenterOfMass(const FVec3& InCenterOfMass, bool bInvalidate = true)
	{
		MMassProps.Modify(bInvalidate, MDirtyFlags, Proxy, [&InCenterOfMass](auto& Data) { Data.SetCenterOfMass(InCenterOfMass); });
	}

	const FRotation3& RotationOfMass() const { return MMassProps.Read().RotationOfMass(); }
	void SetRotationOfMass(const FRotation3& InRotationOfMass, bool bInvalidate = true)
	{
		MMassProps.Modify(bInvalidate, MDirtyFlags, Proxy, [&InRotationOfMass](auto& Data) { Data.SetRotationOfMass(InRotationOfMass); });
	}

	void SetMassProps(const FParticleMassProps& InProps)
	{
		MMassProps.Write(InProps, true, MDirtyFlags, Proxy);
	}

	void SetDynamicMisc(const FParticleDynamicMisc& DynamicMisc)
	{
		MMiscData.Write(DynamicMisc, true, MDirtyFlags, Proxy);
	}

	FReal LinearEtherDrag() const { return MMiscData.Read().LinearEtherDrag(); }
	void SetLinearEtherDrag(const FReal& InLinearEtherDrag)
	{
		MMiscData.Modify(true, MDirtyFlags, Proxy, [&InLinearEtherDrag](auto& Data) { Data.SetLinearEtherDrag(InLinearEtherDrag); });
	}

	FReal AngularEtherDrag() const { return MMiscData.Read().AngularEtherDrag(); }
	void SetAngularEtherDrag(const FReal& InAngularEtherDrag)
	{
		MMiscData.Modify(true, MDirtyFlags, Proxy, [&InAngularEtherDrag](auto& Data) { Data.SetAngularEtherDrag(InAngularEtherDrag); });
	}

	int32 Island() const { return MIsland; }
	// TODO(stett): Make the setter private. It is public right now to provide access to proxies.
	void SetIsland(const int32 InIsland)
	{
		this->MIsland = InIsland;
	}

	bool ToBeRemovedOnFracture() const { return MToBeRemovedOnFracture; }
	// TODO(stett): Make the setter private. It is public right now to provide access to proxies.
	void SetToBeRemovedOnFracture(const bool bToBeRemovedOnFracture)
	{
		this->MToBeRemovedOnFracture = bToBeRemovedOnFracture;
	}

	EObjectStateType ObjectState() const { return MMiscData.Read().ObjectState(); }
	void SetObjectState(const EObjectStateType InState, bool bAllowEvents = false, bool bInvalidate = true)
	{
		if (bAllowEvents)
		{
			const auto PreState = ObjectState();
			if (PreState == EObjectStateType::Dynamic && InState == EObjectStateType::Sleeping)
			{
				MWakeEvent = EWakeEventEntry::Sleep;
			}
			else if (PreState == EObjectStateType::Sleeping && InState == EObjectStateType::Dynamic)
			{
				MWakeEvent = EWakeEventEntry::Awake;
			}
		}

		if (InState == EObjectStateType::Sleeping)
		{
			// When an object is forced into a sleep state, the velocities must be zeroed and buffered,
			// in case the velocity is queried during sleep, or in case the object is woken up again.
			this->SetV(FVec3(0.f), bInvalidate);
			this->SetW(FVec3(0.f), bInvalidate);

			// Dynamic particle properties must be marked clean in order not to actually apply forces which
			// have been buffered. If another force is added after the object is put to sleep, the old forces
			// will remain and the new ones will accumulate and re-dirty the dynamic properties which will
			// wake the body.
			MDirtyFlags.MarkClean(ParticlePropToFlag(EParticleProperty::Dynamics));
		}

		MMiscData.Modify(bInvalidate, MDirtyFlags, Proxy, [&InState](auto& Data) { Data.SetObjectState(InState); });

	}

	void ClearEvents() { MWakeEvent = EWakeEventEntry::None; }
	EWakeEventEntry GetWakeEvent() { return MWakeEvent; }

private:
	TParticleProperty<FParticleMassProps, EParticleProperty::MassProps> MMassProps;
	TParticleProperty<FParticleDynamics, EParticleProperty::Dynamics> MDynamics;
	TParticleProperty<FParticleDynamicMisc, EParticleProperty::DynamicMisc> MMiscData;

	//TUniquePtr<TBVHParticles<T, d>> MCollisionParticles;
	int32 MIsland;
	bool MToBeRemovedOnFracture;
	bool MInitialized;
	EWakeEventEntry MWakeEvent;

protected:
	virtual void SyncRemoteDataImp(FDirtyPropertiesManager& Manager, int32 DataIdx, const FParticleDirtyData& RemoteData) const
	{
		FKinematicGeometryParticleBuffer::SyncRemoteDataImp(Manager, DataIdx, RemoteData);
		MMassProps.SyncRemote(Manager, DataIdx, RemoteData);
		MDynamics.SyncRemote(Manager, DataIdx, RemoteData);
		MMiscData.SyncRemote(Manager, DataIdx, RemoteData);
	}
};

}