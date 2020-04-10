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

template <typename T,EParticleProperty PropName>
class TParticleStateProperty
{
public:

	TParticleStateProperty()
		: Manager(nullptr)
	{
	}

	TParticleStateProperty(FDirtyPropertiesManager* InManager,int32 InIdx)
		: Manager(InManager)
		,Idx(InIdx)
	{
	}

	const T& Read() const
	{
		const TDirtyElementPool<T>& Pool = Manager->GetParticlePool<T,PropName>();
		return Pool.GetElement(Idx);
	}

	template <typename LambdaWrite>
	void SyncToParticle(const LambdaWrite& WriteFunc) const
	{
		if(Manager)
		{
			const TDirtyElementPool<T>& Pool = Manager->GetParticlePool<T,PropName>();
			const T& Value = Pool.GetElement(Idx);
			WriteFunc(Value);
		}
	}

	template <typename LambdaSet>
	void SyncRemoteDataForced(FDirtyPropertiesManager& InManager,int32 InIdx,const LambdaSet& SetFunc)
	{
		Manager = &InManager;
		Idx = InIdx;
		T& NewVal = Manager->GetParticlePool<T,PropName>().GetElement(Idx);
		SetFunc(NewVal);
	}

	template <typename LambdaSet>
	void SyncRemoteData(FDirtyPropertiesManager& InManager,int32 InIdx, const FParticleDirtyData& DirtyData, const LambdaSet& SetFunc)
	{
		if(DirtyData.IsDirty(ParticlePropToFlag(PropName)))
		{
			SyncRemoteDataForced(InManager,InIdx,SetFunc);
		}
	}

	bool IsSet() const
	{
		return Manager != nullptr;
	}

	template <typename TParticleHandle>
	bool IsInSync(const FDirtyPropertiesManager& SrcManager, const int32 DataIdxIn, const FParticleDirtyFlags Flags, const TParticleHandle& Handle) const
	{
		const T* RecordedEntry = Manager ? &GetValue(*Manager,Idx) : nullptr;
		const T* NewEntry = Flags.IsDirty(ParticlePropToFlag(PropName)) ? &GetValue(SrcManager,DataIdxIn) : nullptr;

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
	FDirtyPropertiesManager* Manager;
	int32 Idx;

	static const T& GetValue(const FDirtyPropertiesManager& InManager, const int32 InIdx)
	{
		return InManager.GetParticlePool<T,PropName>().GetElement(InIdx);
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
	TSerializablePtr<FImplicitObject> Geometry(const TParticle& Particle) const
	{
		return NonFrequentData.IsSet() ? MakeSerializable(NonFrequentData.Read().Geometry()) : Particle.Geometry();
	}

	template <typename TParticle>
	const FVec3& F(const TParticle& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().F() : Particle.CastToRigidParticle()->F();
	}

	void SyncSimWritablePropsFromSim(FDirtyPropertiesManager& Manager,int32 Idx,const TPBDRigidParticleHandle<FReal,3>& Rigid)
	{
		FParticleDirtyFlags Flags;
		Flags.MarkDirty(EParticleFlags::XR);
		Flags.MarkDirty(EParticleFlags::Velocities);
		FParticleDirtyData Dirty;
		Dirty.SetFlags(Flags);

		ParticlePositionRotation.SyncRemoteData(Manager,Idx,Dirty,[&Rigid](auto& Data)
		{
			Data.CopyFrom(Rigid);
		});

		Velocities.SyncRemoteData(Manager,Idx,Dirty,[&Rigid](auto& Data)
		{
			Data.SetV(Rigid.PreV());
			Data.SetW(Rigid.PreW());
		});
	}

	void SyncDirtyDynamics(FDirtyPropertiesManager& DestManager,int32 DataIdx,const FParticleDirtyData& Dirty,const FDirtyPropertiesManager& SrcManager)
	{
		FParticleDirtyData DirtyFlags;
		DirtyFlags.SetFlags(Dirty.GetFlags());

		Dynamics.SyncRemoteData(DestManager,DataIdx,DirtyFlags,[&Dirty,&SrcManager,DataIdx](auto& Data)
		{
			Data = Dirty.GetDynamics(SrcManager,DataIdx);
		});
	}

	void SyncToParticle(TGeometryParticle<FReal,3>& Particle) const
	{
		//todo: set properties directly instead of assigning sub-properties

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
			Dynamics.SyncToParticle([Rigid](const auto& Data)
			{
				Rigid->SetDynamics(Data);
			});
		}
	}

	void SyncPrevFrame(FDirtyPropertiesManager& Manager,int32 Idx,const FDirtyProxy& Dirty)
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
			ParticlePositionRotation.SyncRemoteData(Manager,Idx,Dirty.ParticleData,[Handle](FParticlePositionRotation& Data)
			{
				Data.CopyFrom(*Handle);
			});

			if(auto Kinematic = Handle->CastToKinematicParticle())
			{
				Velocities.SyncRemoteData(Manager,Idx,Dirty.ParticleData,[Kinematic](auto& Data)
				{
					Data.CopyFrom(*Kinematic);
				});
			}
		}

		NonFrequentData.SyncRemoteData(Manager,Idx,Dirty.ParticleData,[Handle](FParticleNonFrequentData& Data)
		{
			Data.CopyFrom(*Handle);
		});
	}

	void SyncIfDirty(FDirtyPropertiesManager& Manager,int32 Idx,const TGeometryParticle<FReal,3>& InParticle, const FGeometryParticleStateBase& RewindState)
	{
		ensure(IsInGameThread());
		const auto Particle = &InParticle;

		if(RewindState.ParticlePositionRotation.IsSet())
		{
			ParticlePositionRotation.SyncRemoteDataForced(Manager,Idx,[Particle](FParticlePositionRotation& Data)
			{
				Data.CopyFrom(*Particle);
			});
		}
		
		if(const auto Kinematic = Particle->CastToKinematicParticle())
		{
			if(RewindState.Velocities.IsSet())
			{
				Velocities.SyncRemoteDataForced(Manager,Idx,[Kinematic](auto& Data)
				{
					Data.CopyFrom(*Kinematic);
				});
			}
		}
		
		if(RewindState.NonFrequentData.IsSet())
		{
			NonFrequentData.SyncRemoteDataForced(Manager,Idx,[Particle](FParticleNonFrequentData& Data)
			{
				Data.CopyFrom(*Particle);
			});
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

		//dynamics do not coalesce since they are always written when dirty

		return bCoalesced;
	}

	bool IsDesynced(const FDirtyPropertiesManager& SrcManager, const int32 DataIdxIn, const TGeometryParticleHandle<FReal,3>& Handle, const FParticleDirtyFlags Flags) const
	{
		bool Desynced = false;
		{
			if(!ParticlePositionRotation.IsInSync(SrcManager,DataIdxIn,Flags,Handle))
			{
				return true;
			}
		}

		//TODO: test other properties, should probably find a better way to do this to avoid getting into the individual variables
		
		/*Desynced = ParticlePositionRotation.IsDesynced(SrcManager,DataIdxIn,Dirty);
		Desynced = Desynced || NonFrequentData.IsDesynced(SrcManager,DataIdxIn,Dirty);
		Desynced = Desynced || Velocities.IsDesynced(SrcManager,DataIdxIn,Dirty);
		Desynced = Desynced || Dynamics.IsDesynced(SrcManager,DataIdxIn,Dirty);
		return Desynced;*/
		return false;
	}

private:

	TParticleStateProperty<FParticlePositionRotation,EParticleProperty::XR> ParticlePositionRotation;
	TParticleStateProperty<FParticleNonFrequentData,EParticleProperty::NonFrequentData> NonFrequentData;
	TParticleStateProperty<FParticleVelocities,EParticleProperty::Velocities> Velocities;
	TParticleStateProperty<FParticleDynamics,EParticleProperty::Dynamics> Dynamics;
	/*
	PARTICLE_PROPERTY(XR,FParticlePositionRotation)
		PARTICLE_PROPERTY(Velocities,FParticleVelocities)
		PARTICLE_PROPERTY(Dynamics,FParticleDynamics)
		PARTICLE_PROPERTY(Misc,FParticleMisc)
		PARTICLE_PROPERTY(NonFrequentData,FParticleNonFrequentData)
		PARTICLE_PROPERTY(MassProps,FParticleMassProps)*/
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

	TSerializablePtr<FImplicitObject> Geometry() const
	{
		return State.Geometry(Particle);
	}

	const FVec3& F() const
	{
		return State.F(Particle);
	}

	const TGeometryParticle<FReal,3>& GetParticle() const
	{
		return Particle;
	}

	void SetState(const FGeometryParticleStateBase& InState)
	{
		State = InState;
	}

	bool IsDesynced(const FDirtyPropertiesManager& SrcManager, const int32 DataIdxIn, const TGeometryParticleHandle<FReal,3>& Handle, const FParticleDirtyFlags Flags) const
	{
		return State.IsDesynced(SrcManager,DataIdxIn,Handle,Flags);
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
					LatestState.SyncIfDirty(*DestManager,DataIdx++,*DirtyParticleInfo.Particle,*RewindState);
					CoalesceBack(DirtyParticleInfo.Frames, CurFrame);

					RewindState->SyncToParticle(*DirtyParticleInfo.Particle);
				}
			}
			else
			{
				if(const FGeometryParticleStateBase* RewindState = GetStateAtFrameImp(DirtyParticleInfo,Frame))
				{
					RewindState->SyncToParticle(*DirtyParticleInfo.Particle);
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

	void AdvanceFrame()
	{
		QUICK_SCOPE_CYCLE_COUNTER(RewindDataAdvance);
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
		FDirtyPropertiesManager& DestManager = *Managers[CurFrame].Manager;
		bNeedsSave = true;
		
		auto ProcessProxy = [this,&SrcManager, DestDataIdx, SrcDataIdx, Dirty, &DestManager](const auto Proxy)
		{
			const auto PTParticle = Proxy->GetHandle();
			FDirtyParticleInfo& Info = FindOrAddParticle(*Proxy->GetParticle(),PTParticle->UniqueIdx());
			Info.LastDirtyFrame = CurFrame;
			Info.GTDirtyOnFrame[CurFrame].SetWave(CurFrame,CurWave);

			//check if particle has desynced
			if(bResim)
			{
				FGeometryParticleState FutureState(*Proxy->GetParticle());
				if(GetFutureStateAtFrame(FutureState,CurFrame) == EFutureQueryResult::Ok)
				{
					if(FutureState.IsDesynced(SrcManager, SrcDataIdx, *PTParticle, Dirty.ParticleData.GetFlags()))
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
					LatestState.SyncPrevFrame(DestManager,DestDataIdx,Dirty);
					CoalesceBack(Info.Frames,CurFrame-1);
				}
			}

			//If dynamics are dirty we must record them immediately because the sim resets them to 0
			if(Dirty.ParticleData.IsDirty(EParticleFlags::Dynamics))
			{
				FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame);
				LatestState.SyncDirtyDynamics(DestManager,DestDataIdx,Dirty.ParticleData,SrcManager);
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
	void PushPTDirtyData(const TPBDRigidParticleHandle<FReal,3>& Rigid, const int32 SrcDataIdx)
	{
		const int32 DestDataIdx = SrcDataIdx + DataIdxOffset;

		//todo: is this check needed? why do we pass sleeping rigids into this function?
		if(SimWritablePropsMayChange(Rigid))
		{
			FDirtyParticleInfo& Info = FindOrAddParticle(*Rigid.GTGeometryParticle(), Rigid.UniqueIdx());
			Info.LastDirtyFrame = CurFrame-1;

			//User called PrepareManagerForFrame (or PrepareFrameForPTDirty) for the previous frame, so use it
			FDirtyPropertiesManager& DestManager = *Managers[CurFrame-1].Manager;

			//sim-writable properties changed at head, so we must write down what they were
			FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame-1);
			LatestState.SyncSimWritablePropsFromSim(DestManager,DestDataIdx,Rigid);

			//update any previous frames that were pointing at head
			CoalesceBack(Info.Frames, CurFrame-1);
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
	};

	struct FDirtyParticleInfo
	{
		TCircularBuffer<FFrameInfo> Frames;
		TCircularBuffer<FDirtyFrameInfo> GTDirtyOnFrame;
		TGeometryParticle<FReal,3>* Particle;
		FUniqueIdx CachedUniqueIdx;	//Needed when manipulating on physics thread and Particle data cannot be read
		int32 LastDirtyFrame;	//Track how recently this was made dirty
		bool bDesync;

		FDirtyParticleInfo(TGeometryParticle<FReal,3>& UnsafeGTParticle,const FUniqueIdx UniqueIdx,const int32 CurFrame,const int32 NumFrames)
		: Frames(NumFrames)
		, GTDirtyOnFrame(NumFrames)
		, Particle(&UnsafeGTParticle)
		, CachedUniqueIdx(UniqueIdx)
		, LastDirtyFrame(CurFrame)
		, bDesync(true)
		{

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

	FDirtyParticleInfo& FindOrAddParticle(TGeometryParticle<FReal,3>& UnsafeGTParticle,const FUniqueIdx UniqueIdx)
	{
		if(FDirtyParticleInfo* Info = FindParticle(UniqueIdx))
		{
			return *Info;
		}

		const int32 DirtyIdx = AllDirtyParticles.Add(FDirtyParticleInfo(UnsafeGTParticle,UniqueIdx,CurFrame,Managers.Capacity()));
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
