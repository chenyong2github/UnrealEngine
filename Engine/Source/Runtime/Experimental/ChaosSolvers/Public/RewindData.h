// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Core.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Containers/CircularBuffer.h"

namespace Chaos
{

enum EDesyncResult
{
	InSync, //both have entries and are identical, or both have no entries
	Desync, //both have entries but they are different
	NeedInfo //one of the entries is missing. Need more context to determine whether desynced
};

// Wraps FDirtyPropertiesManager and its DataIdx to avoid confusion between Source and offset Dest indices
struct FDirtyPropData
{
	FDirtyPropData(FDirtyPropertiesManager* InManager, int32 InDataIdx)
		: Ptr(InManager), DataIdx(InDataIdx) { }

	FDirtyPropertiesManager* Ptr;
	int32 DataIdx;
};

struct FConstDirtyPropData
{
	FConstDirtyPropData(const FDirtyPropertiesManager* InManager, int32 InDataIdx)
		: Ptr(InManager), DataIdx(InDataIdx) { }

	const FDirtyPropertiesManager* Ptr;
	int32 DataIdx;
};

template <typename T,EParticleProperty PropName>
class TParticleStateProperty
{
public:
	
	TParticleStateProperty()
		: Manager(nullptr, INDEX_NONE)
	{
	}

	TParticleStateProperty(const FDirtyPropData& InManager)
		: Manager(InManager)
	{
	}

	const T& Read() const
	{
		const TDirtyElementPool<T>& Pool = Manager.Ptr->GetParticlePool<T,PropName>();
		return Pool.GetElement(Manager.DataIdx);
	}

	template <typename LambdaWrite>
	void SyncToParticle(const LambdaWrite& WriteFunc) const
	{
		if(Manager.Ptr)
		{
			const TDirtyElementPool<T>& Pool = Manager.Ptr->GetParticlePool<T,PropName>();
			const T& Value = Pool.GetElement(Manager.DataIdx);
			WriteFunc(Value);
		}
	}

	template <typename LambdaSet>
	void SyncRemoteDataForced(const FDirtyPropData& InManager, const LambdaSet& SetFunc)
	{
		Manager = InManager;
		T& NewVal = Manager.Ptr->GetParticlePool<T,PropName>().GetElement(Manager.DataIdx);
		SetFunc(NewVal);
	}

	template <typename LambdaSet>
	void SyncRemoteData(const FDirtyPropData& InManager, const FParticleDirtyData& DirtyData, const LambdaSet& SetFunc)
	{
		checkSlow(InManager.Ptr != nullptr);
		if(DirtyData.IsDirty(ParticlePropToFlag(PropName)))
		{
			SyncRemoteDataForced(InManager, SetFunc);
		}
	}

	bool IsSet() const
	{
		return Manager.Ptr != nullptr;
	}

	template <typename TParticleHandle>
	bool IsInSync(const FConstDirtyPropData& SrcManager, const FParticleDirtyFlags Flags, const TParticleHandle& Handle) const
	{
		const T* RecordedEntry = Manager.Ptr ? &GetValue(Manager.Ptr, Manager.DataIdx) : nullptr;
		const T* NewEntry = Flags.IsDirty(ParticlePropToFlag(PropName)) ? &GetValue(SrcManager.Ptr, SrcManager.DataIdx) : nullptr;

		if(NewEntry)
		{
			if(RecordedEntry)
			{
				//We have an entry from current run and previous run, so check that they are equal
				return NewEntry->IsEqual(*RecordedEntry);
			}
			else
			{
				//Previous run had no entry. If the current PT data matches the new data, then this is a harmless idnetical write and we are still in sync
				return NewEntry->IsEqual(Handle);
			}
		}
		else
		{
			if(RecordedEntry)
			{
				//We have an entry from previous run, but not anymore. It's possible this will get written out by PT and hasn't yet, so check if the values are the same
				return RecordedEntry->IsEqual(Handle);
			}
			else
			{
				//Both current run and recorded run have no entry, so both pointed at head and saw no change
				return true;
			}
		}
	}

private:
	FDirtyPropData Manager;
	
	static const T& GetValue(const FDirtyPropertiesManager* Ptr, int32 DataIdx)
	{
		return Ptr->GetParticlePool<T,PropName>().GetElement(DataIdx);
	}
};

inline bool SimWritablePropsMayChange(const TGeometryParticleHandle<FReal,3>& Handle)
{
	if(const auto Rigid = Handle.CastToRigidParticle())
	{
		return Rigid->ObjectState() == EObjectStateType::Dynamic;
	}

	return false;
}

class FGeometryParticleStateBase
{
public:
	template <typename TParticle>
	const FVec3& X(const TParticle& Particle) const
	{
		return ParticlePositionRotation.IsSet() ? ParticlePositionRotation.Read().X() : Particle.X();
	}

	template <typename TParticle>
	const FRotation3& R(const TParticle& Particle) const
	{
		return ParticlePositionRotation.IsSet() ? ParticlePositionRotation.Read().R() : Particle.R();
	}
	
	template <typename TParticle>
	const FVec3& V(const TParticle& Particle) const
	{
		return Velocities.IsSet() ? Velocities.Read().V() : Particle.CastToKinematicParticle()->V();
	}

	template <typename TParticle>
	const FVec3& W(const TParticle& Particle) const
	{
		return Velocities.IsSet() ? Velocities.Read().W() : Particle.CastToKinematicParticle()->W();
	}

	template <typename TParticle>
	FReal LinearEtherDrag(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().LinearEtherDrag() : Particle.CastToRigidParticle()->LinearEtherDrag();
	}

	template <typename TParticle>
	FReal AngularEtherDrag(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().AngularEtherDrag() : Particle.CastToRigidParticle()->AngularEtherDrag();
	}

	template <typename TParticle>
	EObjectStateType ObjectState(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().ObjectState() : Particle.CastToRigidParticle()->ObjectState();
	}

	template <typename TParticle>
	bool GravityEnabled(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().GravityEnabled() : Particle.CastToRigidParticle()->GravityEnabled();
	}

	template <typename TParticle>
	int32 CollisionGroup(const TParticle& Particle) const
	{
		return DynamicsMisc.IsSet() ? DynamicsMisc.Read().CollisionGroup() : Particle.CastToRigidParticle()->CollisionGroup();
	}

	template <typename TParticle>
	const FVec3& CenterOfMass(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().CenterOfMass() : Particle.CastToRigidParticle()->CenterOfMass();
	}

	template <typename TParticle>
	const FRotation3& RotationOfMass(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().RotationOfMass() : Particle.CastToRigidParticle()->RotationOfMass();
	}

	template <typename TParticle>
	const FMatrix33& I(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().I() : Particle.CastToRigidParticle()->I();
	}

	template <typename TParticle>
	const FMatrix33& InvI(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().InvI() : Particle.CastToRigidParticle()->InvI();
	}

	template <typename TParticle>
	FReal M(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().M() : Particle.CastToRigidParticle()->M();
	}

	template <typename TParticle>
	FReal InvM(const TParticle& Particle) const
	{
		return MassProps.IsSet() ? MassProps.Read().InvM() : Particle.CastToRigidParticle()->InvM();
	}

	template <typename TParticle>
	TSerializablePtr<FImplicitObject> Geometry(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? MakeSerializable(NonFrequentData.Read().Geometry()) : Particle.Geometry();
	}

	template <typename TParticle>
	void* UserData(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? NonFrequentData.Read().UserData() : Particle.UserData();
	}

	template <typename TParticle>
	FUniqueIdx UniqueIdx(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? NonFrequentData.Read().UniqueIdx() : Particle.UniqueIdx();
	}
	
	template <typename TParticle>
	FSpatialAccelerationIdx SpatialIdx(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? NonFrequentData.Read().SpatialIdx() : Particle.SpatialIdx();
	}

#if CHAOS_CHECKED
	template <typename TParticle>
	FName DebugName(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? NonFrequentData.Read().DebugName() : Particle.DebugName();
	}
#endif

	template <typename TParticle>
	const FVec3& F(const TParticle& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().F() : Particle.CastToRigidParticle()->F();
	}

	template <typename TParticle>
	const FVec3& Torque(const TParticle& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().Torque() : Particle.CastToRigidParticle()->Torque();
	}

	template <typename TParticle>
	const FVec3& LinearImpulse(const TParticle& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().LinearImpulse() : Particle.CastToRigidParticle()->LinearImpulse();
	}

	template <typename TParticle>
	const FVec3& AngularImpulse(const TParticle& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().AngularImpulse() : Particle.CastToRigidParticle()->AngularImpulse();
	}

	void SyncSimWritablePropsFromSim(FDirtyPropData Manager,const TPBDRigidParticleHandle<FReal,3>& Rigid)
	{
		FParticleDirtyFlags Flags;
		Flags.MarkDirty(EParticleFlags::XR);
		Flags.MarkDirty(EParticleFlags::Velocities);
		FParticleDirtyData Dirty;
		Dirty.SetFlags(Flags);

		ParticlePositionRotation.SyncRemoteData(Manager,Dirty,[&Rigid](auto& Data)
		{
			Data.CopyFrom(Rigid);
		});

		Velocities.SyncRemoteData(Manager,Dirty,[&Rigid](auto& Data)
		{
			Data.SetV(Rigid.PreV());
			Data.SetW(Rigid.PreW());
		});
	}

	void SyncDirtyDynamics(FDirtyPropData& DestManager,const FParticleDirtyData& Dirty,const FConstDirtyPropData& SrcManager)
	{
		FParticleDirtyData DirtyFlags;
		DirtyFlags.SetFlags(Dirty.GetFlags());
		
		Dynamics.SyncRemoteData(DestManager,DirtyFlags,[&Dirty,&SrcManager](auto& Data)
		{
			Data = Dirty.GetDynamics(*SrcManager.Ptr,SrcManager.DataIdx);
		});
	}

	template <typename TParticle>
	void SyncToParticle(TParticle& Particle) const
	{
		ParticlePositionRotation.SyncToParticle([&Particle](const auto& Data)
		{
			Particle.SetXR(Data);
		});

		if(auto Kinematic = Particle.CastToKinematicParticle())
		{
			Velocities.SyncToParticle([Kinematic](const auto& Data)
			{
				Kinematic->SetVelocities(Data);
			});
		}

		NonFrequentData.SyncToParticle([&Particle](const auto& Data)
		{
			Particle.SetNonFrequentData(Data);
		});

		if(auto Rigid = Particle.CastToRigidParticle())
		{
			DynamicsMisc.SyncToParticle([Rigid](const auto& Data)
			{
				Rigid->SetDynamicMisc(Data);
			});

			MassProps.SyncToParticle([Rigid](const auto& Data)
			{
				Rigid->SetMassProps(Data);
			});
		}
	}

	void SyncPrevFrame(FDirtyPropData& Manager,const FDirtyProxy& Dirty)
	{
		//syncs the data before it was made dirty
		//for sim-writable props this is only possible if those props are immutable from the sim side (sleeping, not simulated, etc...)

		const auto Proxy = static_cast<const FGeometryParticlePhysicsProxy*>(Dirty.Proxy);
		const auto Handle = Proxy->GetHandle();

		const bool bSyncSimWritable = !SimWritablePropsMayChange(*Handle);

		//note: there is a potential bug here if in one frame we put an object to sleep and change its position
		//not sure if that's a valid operation, we probably need to catch sleep/awake and handle it in a special way
		if(bSyncSimWritable)
		{
			ParticlePositionRotation.SyncRemoteData(Manager,Dirty.ParticleData,[Handle](FParticlePositionRotation& Data)
			{
				Data.CopyFrom(*Handle);
			});

			if(auto Kinematic = Handle->CastToKinematicParticle())
			{
				Velocities.SyncRemoteData(Manager,Dirty.ParticleData,[Kinematic](auto& Data)
				{
					Data.CopyFrom(*Kinematic);
				});
			}
		}

		NonFrequentData.SyncRemoteData(Manager,Dirty.ParticleData,[Handle](FParticleNonFrequentData& Data)
		{
			Data.CopyFrom(*Handle);
		});

		if(auto Rigid = Handle->CastToRigidParticle())
		{
			DynamicsMisc.SyncRemoteData(Manager,Dirty.ParticleData,[Rigid](FParticleDynamicMisc& Data)
			{
				Data.CopyFrom(*Rigid);
			});

			MassProps.SyncRemoteData(Manager,Dirty.ParticleData,[Rigid](FParticleMassProps& Data)
			{
				Data.CopyFrom(*Rigid);
			});
		}
		
	}

	void SyncIfDirty(const FDirtyPropData& Manager,const TGeometryParticle<FReal,3>& InParticle, const FGeometryParticleStateBase& RewindState)
	{
		ensure(IsInGameThread());
		const auto Particle = &InParticle;

		if(RewindState.ParticlePositionRotation.IsSet())
		{
			ParticlePositionRotation.SyncRemoteDataForced(Manager,[Particle](FParticlePositionRotation& Data)
			{
				Data.CopyFrom(*Particle);
			});
		}
		
		if(const auto Kinematic = Particle->CastToKinematicParticle())
		{
			if(RewindState.Velocities.IsSet())
			{
				Velocities.SyncRemoteDataForced(Manager,[Kinematic](auto& Data)
				{
					Data.CopyFrom(*Kinematic);
				});
			}
		}
		
		if(auto Rigid = Particle->CastToRigidParticle())
		{
			if(RewindState.DynamicsMisc.IsSet())
			{
				DynamicsMisc.SyncRemoteDataForced(Manager,[Rigid](FParticleDynamicMisc& Data)
				{
					Data.CopyFrom(*Rigid);
				});
			}

			if(RewindState.MassProps.IsSet())
			{
				MassProps.SyncRemoteDataForced(Manager,[Rigid](FParticleMassProps& Data)
				{
					Data.CopyFrom(*Rigid);
				});
			}
		}
	}

	bool CoalesceState(const FGeometryParticleStateBase& LatestState)
	{
		bool bCoalesced = false;
		if(!ParticlePositionRotation.IsSet() && LatestState.ParticlePositionRotation.IsSet())
		{
			ParticlePositionRotation = LatestState.ParticlePositionRotation;
			bCoalesced = true;
		}

		if(!NonFrequentData.IsSet() && LatestState.NonFrequentData.IsSet())
		{
			NonFrequentData = LatestState.NonFrequentData;
			bCoalesced = true;
		}

		if(!Velocities.IsSet() && LatestState.Velocities.IsSet())
		{
			Velocities = LatestState.Velocities;
			bCoalesced = true;
		}

		if(!MassProps.IsSet() && LatestState.MassProps.IsSet())
		{
			MassProps = LatestState.MassProps;
			bCoalesced = true;
		}

		if(!DynamicsMisc.IsSet() && LatestState.DynamicsMisc.IsSet())
		{
			DynamicsMisc = LatestState.DynamicsMisc;
			bCoalesced = true;
		}

		//dynamics do not coalesce since they are always written when dirty

		return bCoalesced;
	}

	bool IsDesynced(const FConstDirtyPropData& SrcManager, const TGeometryParticleHandle<FReal,3>& Handle, const FParticleDirtyFlags Flags) const
	{
		bool Desynced = false;
		{
			if(!ParticlePositionRotation.IsInSync(SrcManager,Flags,Handle))
			{
				return true;
			}

			if(!NonFrequentData.IsInSync(SrcManager,Flags,Handle))
			{
				return true;
			}

			if(auto Kinematic = Handle.CastToKinematicParticle())
			{
				if(!Velocities.IsInSync(SrcManager,Flags,*Kinematic))
				{
					return true;
				}
			}

			if(auto Rigid = Handle.CastToRigidParticle())
			{
				if(!Dynamics.IsInSync(SrcManager,Flags,*Rigid))
				{
					return true;
				}

				if(!DynamicsMisc.IsInSync(SrcManager,Flags,*Rigid))
				{
					return true;
				}

				if(!MassProps.IsInSync(SrcManager,Flags,*Rigid))
				{
					return true;
				}
			}
		}

		return false;
	}

private:

	TParticleStateProperty<FParticlePositionRotation,EParticleProperty::XR> ParticlePositionRotation;
	TParticleStateProperty<FParticleNonFrequentData,EParticleProperty::NonFrequentData> NonFrequentData;
	TParticleStateProperty<FParticleVelocities,EParticleProperty::Velocities> Velocities;
	TParticleStateProperty<FParticleDynamics,EParticleProperty::Dynamics> Dynamics;
	TParticleStateProperty<FParticleDynamicMisc,EParticleProperty::DynamicMisc> DynamicsMisc;
	TParticleStateProperty<FParticleMassProps,EParticleProperty::MassProps> MassProps;
};

class FGeometryParticleState
{
public:

	FGeometryParticleState(const TGeometryParticle<FReal,3>& InParticle)
	: Particle(InParticle)
	{
	}

	FGeometryParticleState(const FGeometryParticleStateBase& InState, const TGeometryParticle<FReal,3>& InParticle)
	: Particle(InParticle)
	, State(InState)
	{
	}

	const FVec3& X() const
	{
		return State.X(Particle);
	}

	const FRotation3& R() const
	{
		return State.R(Particle);
	}

	const FVec3& V() const
	{
		return State.V(Particle);
	}

	const FVec3& W() const
	{
		return State.W(Particle);
	}

	FReal LinearEtherDrag() const
	{
		return State.LinearEtherDrag(Particle);
	}

	FReal AngularEtherDrag() const
	{
		return State.AngularEtherDrag(Particle);
	}

	EObjectStateType ObjectState() const
	{
		return State.ObjectState(Particle);
	}

	bool GravityEnabled() const
	{
		return State.GravityEnabled(Particle);
	}

	int32 CollisionGroup() const
	{
		return State.CollisionGroup(Particle);
	}

	const FVec3& CenterOfMass() const
	{
		return State.CenterOfMass(Particle);
	}

	const FRotation3& RotationOfMass() const
	{
		return State.RotationOfMass(Particle);
	}

	const FMatrix33& I() const
	{
		return State.I(Particle);
	}

	const FMatrix33& InvI() const
	{
		return State.InvI(Particle);
	}

	FReal M() const
	{
		return State.M(Particle);
	}

	FReal InvM() const
	{
		return State.InvM(Particle);
	}

	TSerializablePtr<FImplicitObject> Geometry() const
	{
		return State.Geometry(Particle);
	}

	void* UserData() const
	{
		return State.UserData(Particle);
	}

	FUniqueIdx UniqueIdx() const
	{
		return State.UniqueIdx(Particle);
	}

	FSpatialAccelerationIdx SpatialIdx() const
	{
		return State.SpatialIdx(Particle);
	}

#if CHAOS_CHECKED
	FName DebugName() const
	{
		return State.DebugName(Particle);
	}
#endif

	const FVec3& F() const
	{
		return State.F(Particle);
	}

	const FVec3& Torque() const
	{
		return State.Torque(Particle);
	}

	const FVec3& LinearImpulse() const
	{
		return State.LinearImpulse(Particle);
	}

	const FVec3& AngularImpulse() const
	{
		return State.AngularImpulse(Particle);
	}

	const TGeometryParticle<FReal,3>& GetParticle() const
	{
		return Particle;
	}

	void SetState(const FGeometryParticleStateBase& InState)
	{
		State = InState;
	}

	bool IsDesynced(const FConstDirtyPropData& SrcManager, const TGeometryParticleHandle<FReal,3>& Handle, const FParticleDirtyFlags Flags) const
	{
		return State.IsDesynced(SrcManager,Handle,Flags);
	}

private:
	const TGeometryParticle<FReal,3>& Particle;
	FGeometryParticleStateBase State;
};

enum class EFutureQueryResult
{
	Ok,	//There is reliable data for this particle
	Untracked, //The particle is untracked. This could mean it's new, or that it was unchanged in prior simulations
	Desync //The particle's state has diverged from the previous recordings
};

class FRewindData
{
public:
	FRewindData(int32 NumFrames)
	: Managers(NumFrames+1)	//give 1 extra for saving at head
	, CurFrame(0)
	, LatestFrame(0)
	, CurWave(1)
	, FramesSaved(0)
	, DataIdxOffset(0)
	, bNeedsSave(false)
	{
	}

	int32 Capacity() const { return Managers.Capacity(); }
	int32 CurrentFrame() const { return CurFrame; }

	FReal GetDeltaTimeForFrame(int32 Frame) const
	{
		ensure(Managers[Frame].FrameCreatedFor == Frame);
		return Managers[Frame].DeltaTime;
	}

	bool RewindToFrame(int32 Frame)
	{
		ensure(IsInGameThread());

		//Can't go too far back
		const int32 EarliestFrame = CurFrame - FramesSaved;
		if(Frame < EarliestFrame)
		{
			return false;
		}

		//If we need to save and we are right on the edge of the buffer, we can't go back to earliest frame
		if(Frame == EarliestFrame && bNeedsSave && FramesSaved == Managers.Capacity())
		{
			return false;
		}
		
		FDirtyPropertiesManager* DestManager = nullptr;
		if(bNeedsSave)
		{
			PrepareFrame(AllDirtyParticles.Num());
			DestManager = Managers[CurFrame].Manager.Get();
		}

		//todo: parallel for
		int32 DataIdx = 0;
		for(FDirtyParticleInfo& DirtyParticleInfo : AllDirtyParticles)
		{
			DirtyParticleInfo.bDesync = false;	//after rewind particle is pristine

			if(bNeedsSave)
			{
				//GetStateAtFrameImp returns a pointer from the TArray that holds state data
				//But it's possible that we'll need to save state from head, which would grow that TArray
				//So preallocate just in case
				FGeometryParticleStateBase& LatestState = DirtyParticleInfo.AddFrame(CurFrame);
			
				if(const FGeometryParticleStateBase* RewindState = GetStateAtFrameImp(DirtyParticleInfo, Frame))
				{

					LatestState.SyncIfDirty(FDirtyPropData(DestManager,DataIdx++),*DirtyParticleInfo.GetGTParticle(),*RewindState);
					CoalesceBack(DirtyParticleInfo.Frames, CurFrame);

					RewindState->SyncToParticle(*DirtyParticleInfo.GetGTParticle());
				}
			}
			else
			{
				if(const FGeometryParticleStateBase* RewindState = GetStateAtFrameImp(DirtyParticleInfo,Frame))
				{
					RewindState->SyncToParticle(*DirtyParticleInfo.GetGTParticle());
				}
			}
		}

		CurFrame = Frame;
		bNeedsSave = false;
		FramesSaved = 0; //can't rewind before this point. This simplifies saving the state at head
		++CurWave;
		if(CurWave == 0)
		{
			//0 indicates nothing written so skip it
			CurWave = 1;
		}

		return true;
	}

	void RemoveParticle(const FUniqueIdx UniqueIdx)
	{
		if(const int32* IdxPtr = ParticleToAllDirtyIdx.Find(UniqueIdx))
		{
			const int32 Idx = *IdxPtr;
			AllDirtyParticles.RemoveAtSwap(Idx);
			if(Idx < AllDirtyParticles.Num())
			{
				//update particle in new position
				ParticleToAllDirtyIdx.FindChecked(AllDirtyParticles[Idx].CachedUniqueIdx) = Idx;
			}

			ParticleToAllDirtyIdx.RemoveChecked(UniqueIdx);
		}
	}

	/* Query the state of particles from the past. Once a rewind happens state captured must be queried using GetFutureStateAtFrame */
	FGeometryParticleState GetPastStateAtFrame(const TGeometryParticle<FReal,3>& Particle,int32 Frame) const
	{
		ensure(!IsResim());
		if(const FDirtyParticleInfo* Info = FindParticle(Particle.UniqueIdx()))
		{
			if(const FGeometryParticleStateBase* State = GetStateAtFrameImp(*Info,Frame))
			{
				return FGeometryParticleState(*State,Particle);
			}
		}

		//If no data, or past capture, just use head
		return FGeometryParticleState(Particle);
	}

	/* Query the state of particles in the future. This operation can fail for particles that are desynced or that we have not been tracking */
	EFutureQueryResult GetFutureStateAtFrame(FGeometryParticleState& OutState,int32 Frame) const
	{
		ensure(IsResim());
		ensure(IsInGameThread());
		const TGeometryParticle<FReal,3>& Particle = OutState.GetParticle();

		if(const FDirtyParticleInfo* Info = FindParticle(Particle.UniqueIdx()))
		{
			if(Info->bDesync)
			{
				return EFutureQueryResult::Desync;
			}

			if(const FGeometryParticleStateBase* State = GetStateAtFrameImp(*Info,Frame))
			{
				OutState.SetState(*State);
				return EFutureQueryResult::Ok;
			}
		}

		return EFutureQueryResult::Untracked;
	}

	void AdvanceFrame(FReal DeltaTime)
	{
		QUICK_SCOPE_CYCLE_COUNTER(RewindDataAdvance);
		Managers[CurFrame].DeltaTime = DeltaTime;

		++CurFrame;
		LatestFrame = FMath::Max(LatestFrame,CurFrame);
		FramesSaved = FMath::Min(FramesSaved+1,static_cast<int32>(Managers.Capacity()));
		
		const int32 EarliestFrame = CurFrame - 1 - FramesSaved;
		//remove any old dirty particles
		for(int32 DirtyIdx = AllDirtyParticles.Num() - 1; DirtyIdx >= 0; --DirtyIdx)
		{
			FDirtyParticleInfo& Info = AllDirtyParticles[DirtyIdx];
			if(Info.LastDirtyFrame < EarliestFrame)
			{
				RemoveParticle(AllDirtyParticles[DirtyIdx].CachedUniqueIdx);
			}
			else if(IsResim())
			{
				//During a resim it's possible the user will not dirty a particle that was previously dirty.
				//If this happens we need to mark the particle as desynced
				if(!Info.bDesync && Info.GTDirtyOnFrame[CurFrame-1].MissingWrite(CurFrame-1, CurWave))
				{
					Info.Desync(CurFrame-1, LatestFrame);
				}

				if(auto* Rigid = Info.GetPTParticle()->CastToRigidParticle())
				{
					if(Rigid->ResimType() == EResimType::SimAsSlave)
					{
						//TODO: Need to reapply forces automatically
					}
				}
			}
		}
	}

	void FinishFrame()
	{
		QUICK_SCOPE_CYCLE_COUNTER(RewindDataFinishFrame);
		if(IsResim())
		{
			ensure(IsInGameThread());
			//snap particles forward that are not desynced or  do not have resim enabled
			//TODO: handle desync case
			for(FDirtyParticleInfo& Info : AllDirtyParticles)
			{
				if(auto* Rigid = Info.GetPTParticle()->CastToRigidParticle())
				{
					if(Rigid->ResimType() == EResimType::SimAsSlave)
					{
						ensure(!Info.bDesync);
						const FGeometryParticleStateBase* State = GetStateAtFrameImp(Info,CurFrame);
						if(ensure(State != nullptr))
						{
							State->SyncToParticle(*Rigid);
						}
					}
				}
			}
		}
		
	}

	bool IsResim() const
	{
		return CurFrame < LatestFrame;
	}

	//Number of particles that we're currently storing history for
	int32 GetNumDirtyParticles() const { return AllDirtyParticles.Num(); }

	void PrepareFrame(int32 NumDirtyParticles)
	{
		FFrameManagerInfo& Info = Managers[CurFrame];
		if(Info.Manager == nullptr)
		{
			Info.Manager = MakeUnique<FDirtyPropertiesManager>();
		}

		DataIdxOffset = Info.Manager->GetNumParticles();
		Info.Manager->SetNumParticles(DataIdxOffset + NumDirtyParticles);
		Info.FrameCreatedFor = CurFrame;
	}

	void PrepareFrameForPTDirty(int32 NumActiveParticles)
	{
		bNeedsSave = true;

		//If manager already exists for previous frame, use it
		const int32 PrevFrame = CurFrame - 1;
		FFrameManagerInfo& Info = Managers[PrevFrame];
		ensure(Info.Manager && Info.FrameCreatedFor == (PrevFrame));

		DataIdxOffset = Info.Manager->GetNumParticles();
		Info.Manager->SetNumParticles(DataIdxOffset + NumActiveParticles);
	}

	template <bool bResim>
	void PushGTDirtyData(const FDirtyPropertiesManager& SrcManager, const int32 SrcDataIdx, const FDirtyProxy& Dirty)
	{
		const int32 DestDataIdx = SrcDataIdx + DataIdxOffset;
		//This records changes enqueued by GT.
		//Most new particles do not change, so to avoid useless writes we wait until the next frame's dirty flag
		//This is possible because most properties are const on the physics thread
		//For sim-writable properties (forces, position, velocities, etc...) we must immediately write the data because there is no way to know what the previous data was next frame
		//Some sim-writable properties can change without the GT knowing about it, see PushPTDirtyData

		//User called PrepareManagerForFrame for this frame so use it
		FConstDirtyPropData SrcManagerWrapper(&SrcManager, SrcDataIdx);
		FDirtyPropData DestManagerWrapper(Managers[CurFrame].Manager.Get(), DestDataIdx);
		bNeedsSave = true;
		
		auto ProcessProxy = [this,&SrcManagerWrapper, SrcDataIdx, Dirty, &DestManagerWrapper](const auto Proxy)
		{
			const auto PTParticle = Proxy->GetHandle();
			FDirtyParticleInfo& Info = FindOrAddParticle(*PTParticle);
			Info.LastDirtyFrame = CurFrame;
			Info.GTDirtyOnFrame[CurFrame].SetWave(CurFrame,CurWave);

			//check if particle has desynced
			if(bResim)
			{
				FGeometryParticleState FutureState(*Proxy->GetParticle());
				if(GetFutureStateAtFrame(FutureState,CurFrame) == EFutureQueryResult::Ok)
				{
					if(FutureState.IsDesynced(SrcManagerWrapper, *PTParticle, Dirty.ParticleData.GetFlags()))
					{
						Info.Desync(CurFrame-1, LatestFrame);
					}
				}
				else if(!Info.bDesync)
				{
					Info.Desync(CurFrame-1, LatestFrame);
				}
			}

			//Most properties are always a frame behind
			if(Proxy->IsInitialized())	//Frame delay so proxy must be initialized
			{
				//If we're in a resim and this is the first frame or the resim, no need to save prev frame
				//In fact, since we have a circular buffer the prev state could end up overwriting head which we need for fast forward
				if(!bResim || FramesSaved > 0)
				{
					FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame-1);
					LatestState.SyncPrevFrame(DestManagerWrapper,Dirty);
					CoalesceBack(Info.Frames,CurFrame-1);
				}
			}

			//If dynamics are dirty we must record them immediately because the sim resets them to 0
			if(Dirty.ParticleData.IsDirty(EParticleFlags::Dynamics))
			{
				FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame);
				LatestState.SyncDirtyDynamics(DestManagerWrapper,Dirty.ParticleData,SrcManagerWrapper);
			}
		};

		switch(Dirty.Proxy->GetType())
		{
		case EPhysicsProxyType::SingleRigidParticleType:
		{
			auto Proxy = static_cast<FRigidParticlePhysicsProxy*>(Dirty.Proxy);
			ProcessProxy(Proxy);
			break;
		}
		case EPhysicsProxyType::SingleKinematicParticleType:
		{
			auto Proxy = static_cast<FKinematicGeometryParticlePhysicsProxy*>(Dirty.Proxy);
			ProcessProxy(Proxy);
			break;
		}
		case EPhysicsProxyType::SingleGeometryParticleType:
		{
			auto Proxy = static_cast<FGeometryParticlePhysicsProxy*>(Dirty.Proxy);
			ProcessProxy(Proxy);
			break;
		}
		default:
		ensure("Unknown proxy type in physics solver.");
		}
	}

	template <bool bResim>
	void PushPTDirtyData(TPBDRigidParticleHandle<FReal,3>& Rigid, const int32 SrcDataIdx)
	{
		const int32 DestDataIdx = SrcDataIdx + DataIdxOffset;

		if(bResim && Rigid.ResimType() == EResimType::SimAsSlave)
		{
			//resim not allowed for this particle so don't modify
		}
		else
		{
			//todo: is this check needed? why do we pass sleeping rigids into this function?
			if(SimWritablePropsMayChange(Rigid))
			{
				FDirtyParticleInfo& Info = FindOrAddParticle(Rigid);
				Info.LastDirtyFrame = CurFrame-1;

				//User called PrepareManagerForFrame (or PrepareFrameForPTDirty) for the previous frame, so use it
				FDirtyPropData DestManagerWrapper(Managers[CurFrame-1].Manager.Get(), DestDataIdx);

				//sim-writable properties changed at head, so we must write down what they were
				FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame-1);
				LatestState.SyncSimWritablePropsFromSim(DestManagerWrapper,Rigid);

				//update any previous frames that were pointing at head
				CoalesceBack(Info.Frames, CurFrame-1);
			}
		}
	}

private:

	struct FDirtyFrameInfo
	{
		int32 Frame;	//needed to protect against stale entries in circular buffer
		uint8 Wave;

		void SetWave(int32 InFrame, uint8 InWave)
		{
			Frame = InFrame;
			Wave = InWave;
		}

		bool MissingWrite(int32 InFrame, uint8 InWave) const
		{
			//If this is not a stale entry and it was written to, but not during this latest sim
			return (Wave != 0 && Frame == InFrame) && Wave != InWave;
		}
	};

	class FFrameInfo
	{
	public:
		FFrameInfo()
		: bSet(false)
		{

		}

		FGeometryParticleStateBase* GetState(int32 Frame)
		{
			return (bSet && Frame == RecordedFrame) ? &State : nullptr;
		}

		const FGeometryParticleStateBase* GetState(int32 Frame) const
		{
			return (bSet && Frame == RecordedFrame) ? &State : nullptr;
		}

		FGeometryParticleStateBase& GetStateChecked(int32 Frame)
		{
			check(bSet && Frame == RecordedFrame);
			return State;
		}

		const FGeometryParticleStateBase& GetStateChecked(int32 Frame) const
		{
			check(bSet && Frame == RecordedFrame);
			return State;
		}

		FGeometryParticleStateBase& NewState(int32 Frame)
		{
			RecordedFrame = Frame;
			bSet = true;
			State = FGeometryParticleStateBase();
			return State;
		}

		void ClearState()
		{
			bSet = false;
		}

	private:
		FGeometryParticleStateBase State;
		int32 RecordedFrame;
		bool bSet;
	};

	void CoalesceBack(TCircularBuffer<FFrameInfo>& Frames, int32 LatestIdx)
	{
		const FGeometryParticleStateBase& LatestState = Frames[LatestIdx].GetStateChecked(LatestIdx);
		const int32 EarliestFrame = LatestIdx - FramesSaved;
		for(int32 FrameIdx = LatestIdx - 1; FrameIdx >= EarliestFrame; --FrameIdx)
		{
			if(FGeometryParticleStateBase* State = Frames[FrameIdx].GetState(FrameIdx))
			{
				if(State->CoalesceState(LatestState) == false)
				{
					//nothing to coalesce so no need to check earlier frames
					break;
				}
			}
		}
	}
	
	struct FFrameManagerInfo
	{
		TUniquePtr<FDirtyPropertiesManager> Manager;

		//Note that this is not exactly the same as which frame this manager represents. 
		//A manager can have data for two frames at once, the important part is just knowing which frame it was created on so we know whether the physics data can rely on it
		//Consider the case where nothing is dirty from GT and then an object moves from the simulation, in that case it needs a manager to record the data into
		int32 FrameCreatedFor;
		FReal DeltaTime;
	};

	struct FDirtyParticleInfo
	{
		TCircularBuffer<FFrameInfo> Frames;
		TCircularBuffer<FDirtyFrameInfo> GTDirtyOnFrame;
	private:
		TGeometryParticle<FReal,3>* GTParticle;
		TGeometryParticleHandle<FReal,3>* PTParticle;
	public:
		FUniqueIdx CachedUniqueIdx;	//Needed when manipulating on physics thread and Particle data cannot be read
		int32 LastDirtyFrame;	//Track how recently this was made dirty
		bool bDesync;

		FDirtyParticleInfo(TGeometryParticle<FReal,3>& UnsafeGTParticle, TGeometryParticleHandle<FReal,3>& InPTParticle, const FUniqueIdx UniqueIdx,const int32 CurFrame,const int32 NumFrames)
		: Frames(NumFrames)
		, GTDirtyOnFrame(NumFrames)
		, GTParticle(&UnsafeGTParticle)
		, PTParticle(&InPTParticle)
		, CachedUniqueIdx(UniqueIdx)
		, LastDirtyFrame(CurFrame)
		, bDesync(true)
		{

		}

		TGeometryParticle<FReal,3>* GetGTParticle() const
		{
			ensure(IsInGameThread());
			return GTParticle;
		}

		TGeometryParticleHandle<FReal,3>* GetPTParticle() const
		{
			return PTParticle;
		}

		FGeometryParticleStateBase& AddFrame(int32 FrameIdx)
		{
			FFrameInfo& Info = Frames[FrameIdx];
			if(FGeometryParticleStateBase* State = Info.GetState(FrameIdx))
			{
				return *State;
			}

			return Info.NewState(FrameIdx);
		}

		void Desync(int32 StartDesync, int32 LastFrame)
		{
			bDesync = true;
			for(int32 Frame = StartDesync; Frame <= LastFrame; ++Frame)
			{
				Frames[Frame].ClearState();
			}
		}
	};

	const FGeometryParticleStateBase* GetStateAtFrameImp(const FDirtyParticleInfo& Info,int32 Frame) const
	{
		const TCircularBuffer<FFrameInfo>& Frames = Info.Frames;
		if(const FGeometryParticleStateBase* FrameState = Frames[Frame].GetState(Frame))
		{
			return FrameState;
		}

		//If frame is between two captures, use later capture. We always store the last data before a change
		//We can never use an earlier capture because the fact that we captured at all implies _something_ is different from proceeding frames

		for(int32 FrameIdx = Frame + 1; FrameIdx <= LatestFrame; ++FrameIdx)
		{
			if(const FGeometryParticleStateBase* FrameState = Frames[FrameIdx].GetState(FrameIdx))
			{
				return FrameState;
			}
		}

		//If no data, or past capture, just use head
		return nullptr;
	}

	const FDirtyParticleInfo& FindParticleChecked(const FUniqueIdx UniqueIdx) const
	{
		const int32 Idx = ParticleToAllDirtyIdx.FindChecked(UniqueIdx);
		return AllDirtyParticles[Idx];
	}

	FDirtyParticleInfo& FindParticleChecked(const FUniqueIdx UniqueIdx)
	{
		const int32 Idx = ParticleToAllDirtyIdx.FindChecked(UniqueIdx);
		return AllDirtyParticles[Idx];
	}

	const FDirtyParticleInfo* FindParticle(const FUniqueIdx UniqueIdx) const
	{
		if(const int32* Idx = ParticleToAllDirtyIdx.Find(UniqueIdx))
		{
			return &AllDirtyParticles[*Idx];
		}

		return nullptr;
	}

	FDirtyParticleInfo* FindParticle(const FUniqueIdx UniqueIdx)
	{
		if(const int32* Idx = ParticleToAllDirtyIdx.Find(UniqueIdx))
		{
			return &AllDirtyParticles[*Idx];
		}

		return nullptr;
	}

	FDirtyParticleInfo& FindOrAddParticle(TGeometryParticleHandle<FReal,3>& PTParticle)
	{
		const FUniqueIdx UniqueIdx = PTParticle.UniqueIdx();
		if(FDirtyParticleInfo* Info = FindParticle(UniqueIdx))
		{
			return *Info;
		}

		auto* GTUnsafeParticle = PTParticle.GTGeometryParticle();
		ensure(GTUnsafeParticle != nullptr);
		const int32 DirtyIdx = AllDirtyParticles.Add(FDirtyParticleInfo(*GTUnsafeParticle, PTParticle,UniqueIdx,CurFrame,Managers.Capacity()));
		ParticleToAllDirtyIdx.Add(UniqueIdx,DirtyIdx);

		return AllDirtyParticles[DirtyIdx];
	}

	TArrayAsMap<FUniqueIdx,int32> ParticleToAllDirtyIdx;
	TCircularBuffer<FFrameManagerInfo> Managers;
	TArray<FDirtyParticleInfo> AllDirtyParticles;
	int32 CurFrame;
	int32 LatestFrame;
	uint8 CurWave;
	int32 FramesSaved;
	int32 DataIdxOffset;
	bool bNeedsSave;	//Indicates that some data is pointing at head and requires saving before a rewind
};
}
