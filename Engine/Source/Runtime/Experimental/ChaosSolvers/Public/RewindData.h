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
		SetFunc(Manager->GetParticlePool<T,PropName>().GetElement(Idx));
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

private:
	FDirtyPropertiesManager* Manager;
	int32 Idx;
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
	const FVec3& X(const TGeometryParticle<FReal,3>& Particle) const
	{
		return ParticlePositionRotation.IsSet() ? ParticlePositionRotation.Read().X : Particle.X();
	}

	const FRotation3& R(const TGeometryParticle<FReal,3>& Particle) const
	{
		return ParticlePositionRotation.IsSet() ? ParticlePositionRotation.Read().R : Particle.R();
	}

	const FVec3& V(const TGeometryParticle<FReal,3>& Particle) const
	{
		return Velocities.IsSet() ? Velocities.Read().V : Particle.CastToKinematicParticle()->V();
	}

	const FVec3& W(const TGeometryParticle<FReal,3>& Particle) const
	{
		return Velocities.IsSet() ? Velocities.Read().W : Particle.CastToKinematicParticle()->W();
	}

	TSerializablePtr<FImplicitObject> Geometry(const TGeometryParticle<FReal,3>& Particle) const
	{
		return NonFrequentData.IsSet() ? MakeSerializable(NonFrequentData.Read().Geometry) : Particle.Geometry();
	}

	const FVec3& F(const TGeometryParticle<FReal,3>& Particle) const
	{
		return Dynamics.IsSet() ? Dynamics.Read().F : Particle.CastToRigidParticle()->F();
	}

	bool SyncSimWritablePropsFromSim(FDirtyPropertiesManager& Manager,int32 Idx,const TPBDRigidParticleHandle<FReal,3>& Rigid)
	{
		FParticleDirtyFlags Flags;
		Flags.MarkDirty(EParticleFlags::XR);
		Flags.MarkDirty(EParticleFlags::Velocities);
		FParticleDirtyData Dirty;
		Dirty.SetFlags(Flags);

		ParticlePositionRotation.SyncRemoteData(Manager,Idx,Dirty,[&Rigid](auto& Data)
		{
			Data.X = Rigid.X();
			Data.R = Rigid.R();
		});

		Velocities.SyncRemoteData(Manager,Idx,Dirty,[&Rigid](auto& Data)
		{
			Data.V = Rigid.PreV();
			Data.W = Rigid.PreW();
		});

		return true;	//todo: check if values are actually different
	}

	bool SyncDirtyDynamics(FDirtyPropertiesManager& DestManager,int32 DataIdx,const FParticleDirtyData& Dirty,const FDirtyPropertiesManager& SrcManager)
	{
		FParticleDirtyData DirtyFlags;
		DirtyFlags.SetFlags(Dirty.GetFlags());

		Dynamics.SyncRemoteData(DestManager,DataIdx,DirtyFlags,[&Dirty,&SrcManager,DataIdx](auto& Data)
		{
			Data = Dirty.GetDynamics(SrcManager,DataIdx);
		});

		return true;	//todo: check if values are actually different
	}

	void SyncToParticle(TGeometryParticle<FReal,3>& Particle) const
	{
		//todo: set properties directly instead of assigning sub-properties

		ParticlePositionRotation.SyncToParticle([&Particle](const auto& Data)
		{
			Particle.SetX(Data.X);
			Particle.SetR(Data.R);
		});

		if(auto Kinematic = Particle.CastToKinematicParticle())
		{
			Velocities.SyncToParticle([Kinematic](const auto& Data)
			{
				Kinematic->SetV(Data.V);
				Kinematic->SetW(Data.W);
			});
		}


		NonFrequentData.SyncToParticle([&Particle](const auto& Data)
		{
			Particle.SetGeometry(Data.Geometry);
			Particle.SetUserData(Data.UserData);
			ensure(Data.UniqueIdx == Particle.UniqueIdx());	//this should never change

#if CHAOS_CHECKED
			Particle.SetDebugName(Data.DebugName);
#endif
			if(auto Rigid = Particle.CastToRigidParticle())
			{
				Rigid->SetLinearEtherDrag(Data.LinearEtherDrag);
				Rigid->SetAngularEtherDrag(Data.AngularEtherDrag);
			}
		});

		if(auto Rigid = Particle.CastToRigidParticle())
		{
			Dynamics.SyncToParticle([Rigid](const auto& Data)
			{
				Rigid->SetF(Data.F);
				Rigid->SetTorque(Data.Torque);
				Rigid->SetLinearImpulse(Data.LinearImpulse);
				Rigid->SetAngularImpulse(Data.AngularImpulse);
			});
		}
	}

	bool SyncPrevFrame(FDirtyPropertiesManager& Manager,int32 Idx,const FDirtyProxy& Dirty)
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
				Data.X = Handle->X();
				Data.R = Handle->R();
			});

			if(auto Kinematic = Handle->CastToKinematicParticle())
			{
				Velocities.SyncRemoteData(Manager,Idx,Dirty.ParticleData,[Kinematic](auto& Data)
				{
					Data.V = Kinematic->V();
					Data.W = Kinematic->W();
				});
			}
		}

		NonFrequentData.SyncRemoteData(Manager,Idx,Dirty.ParticleData,[Handle](FParticleNonFrequentData& Data)
		{
			Data.Geometry = Handle->SharedGeometryLowLevel();
			Data.UserData = Handle->UserData();

			//note: this data is keyed based on unique idx so it's not really possible to change this
			//but we save it anyway since it's part of a big struct
			Data.UniqueIdx = Handle->UniqueIdx();
#if CHAOS_CHECKED
			Data.DebugName = Handle->DebugName();
#endif

			if(auto Rigid = Handle->CastToRigidParticle())
			{
				Data.LinearEtherDrag = Rigid->LinearEtherDrag();
				Data.AngularEtherDrag = Rigid->AngularEtherDrag();
			}
		});

		//todo: check whether the data actually changed
		return true;
	}

	void SyncIfDirty(FDirtyPropertiesManager& Manager,int32 Idx,const TGeometryParticle<FReal,3>& InParticle, const FGeometryParticleStateBase& RewindState)
	{
		ensure(IsInGameThread());
		const auto Particle = &InParticle;

		if(RewindState.ParticlePositionRotation.IsSet())
		{
			ParticlePositionRotation.SyncRemoteDataForced(Manager,Idx,[Particle](FParticlePositionRotation& Data)
			{
				Data.X = Particle->X();
				Data.R = Particle->R();
			});
		}
		
		if(const auto Kinematic = Particle->CastToKinematicParticle())
		{
			if(RewindState.Velocities.IsSet())
			{
				Velocities.SyncRemoteDataForced(Manager,Idx,[Kinematic](auto& Data)
				{
					Data.V = Kinematic->V();
					Data.W = Kinematic->W();
				});
			}
		}
		
		if(RewindState.NonFrequentData.IsSet())
		{
			NonFrequentData.SyncRemoteDataForced(Manager,Idx,[Particle](FParticleNonFrequentData& Data)
			{
				Data.Geometry = Particle->SharedGeometryLowLevel();
				Data.UserData = Particle->UserData();

				//note: this data is keyed based on unique idx so it's not really possible to change this
				//but we save it anyway since it's part of a big struct
				Data.UniqueIdx = Particle->UniqueIdx();
	#if CHAOS_CHECKED
				Data.DebugName = Particle->DebugName();
	#endif

				if(auto Rigid = Particle->CastToRigidParticle())
				{
					Data.LinearEtherDrag = Rigid->LinearEtherDrag();
					Data.AngularEtherDrag = Rigid->AngularEtherDrag();
				}
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

class FGeometryParticleState : private FGeometryParticleStateBase
{
public:

	FGeometryParticleState(const TGeometryParticle<FReal,3>& InParticle)
	: Particle(InParticle)
	{
	}

	FGeometryParticleState(const FGeometryParticleStateBase& State, const TGeometryParticle<FReal,3>& InParticle)
	: FGeometryParticleStateBase(State)
	, Particle(InParticle)
	{
	}

	const FVec3& X() const
	{
		return FGeometryParticleStateBase::X(Particle);
	}

	const FRotation3& R() const
	{
		return FGeometryParticleStateBase::R(Particle);
	}

	const FVec3& V() const
	{
		return FGeometryParticleStateBase::V(Particle);
	}

	const FVec3& W() const
	{
		return FGeometryParticleStateBase::W(Particle);
	}

	TSerializablePtr<FImplicitObject> Geometry() const
	{
		return FGeometryParticleStateBase::Geometry(Particle);
	}

	const FVec3& F() const
	{
		return FGeometryParticleStateBase::F(Particle);
	}

private:
	const TGeometryParticle<FReal,3>& Particle;
};

class FRewindData
{
public:
	FRewindData(int32 NumFrames)
	: Managers(NumFrames+1)	//give 1 extra for saving at head
	, CurFrame(0)
	, CurWave(0)
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
				FGeometryParticleStateBase& LatestState = DirtyParticleInfo.AddFrame(CurFrame, CurWave);
			
				if(const FGeometryParticleStateBase* RewindState = GetStateAtFrameImp(*DirtyParticleInfo.Particle, Frame))
				{
					LatestState.SyncIfDirty(*DestManager,DataIdx++,*DirtyParticleInfo.Particle,*RewindState);
					CoalesceBack(DirtyParticleInfo.Frames, CurFrame);

					RewindState->SyncToParticle(*DirtyParticleInfo.Particle);
				}
			}
			else
			{
				if(const FGeometryParticleStateBase* RewindState = GetStateAtFrameImp(*DirtyParticleInfo.Particle,Frame))
				{
					RewindState->SyncToParticle(*DirtyParticleInfo.Particle);
				}
			}
		}

		CurFrame = Frame;
		bNeedsSave = false;
		FramesSaved = 0; //can't rewind before this point. This simplifies saving the state at head

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

	FGeometryParticleState GetStateAtFrame(const TGeometryParticle<FReal,3>& Particle,int32 Frame) const
	{
		if(const FGeometryParticleStateBase* State = GetStateAtFrameImp(Particle, Frame))
		{
			return FGeometryParticleState(*State,Particle);
		}

		//If no data, or past capture, just use head
		return FGeometryParticleState(Particle);
	}

	void AdvanceFrame()
	{
		QUICK_SCOPE_CYCLE_COUNTER(RewindDataAdvance);
		++CurFrame;
		FramesSaved = FMath::Min(FramesSaved+1,static_cast<int32>(Managers.Capacity()));
		
		const int32 EarliestFrame = CurFrame - 1 - FramesSaved;
		//remove any old dirty particles
		for(int32 DirtyIdx = AllDirtyParticles.Num() - 1; DirtyIdx >= 0; --DirtyIdx)
		{
			if(AllDirtyParticles[DirtyIdx].LastDirtyFrame < EarliestFrame)
			{
				RemoveParticle(AllDirtyParticles[DirtyIdx].CachedUniqueIdx);
			}
		}
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

	void PushGTDirtyData(const FDirtyPropertiesManager& SrcManager, const int32 DataIdxIn, const FDirtyProxy& Dirty)
	{
		const int32 DataIdx = DataIdxIn + DataIdxOffset;
		//This records changes enqueued by GT.
		//Most new particles do not change, so to avoid useless writes we wait until the next frame's dirty flag
		//This is possible because most properties are const on the physics thread
		//For sim-writable properties (forces, position, velocities, etc...) we must immediately write the data because there is no way to know what the previous data was next frame
		//Some sim-writable properties can change without the GT knowing about it, see PushPTDirtyData

		//User called PrepareManagerForFrame for this frame so use it
		FDirtyPropertiesManager& DestManager = *Managers[CurFrame].Manager;
		bNeedsSave = true;
		
		auto ProcessProxy = [this,&SrcManager, DataIdx, Dirty, &DestManager](const auto Proxy)
		{
			const auto PTParticle = Proxy->GetHandle();
			FDirtyParticleInfo& Info = FindOrAddParticle(*Proxy->GetParticle(),PTParticle->UniqueIdx());
			Info.LastDirtyFrame = CurFrame;

			bool bChanged = false;

			//Most properties are always a frame behind
			if(Proxy->IsInitialized())	//Frame delay so proxy must be initialized
			{
				FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame-1, CurWave);
				bChanged = LatestState.SyncPrevFrame(DestManager,DataIdx,Dirty);
				CoalesceBack(Info.Frames, CurFrame-1);
			}

			//If dynamics are dirty we must record them immediately because the sim resets them to 0
			if(Dirty.ParticleData.IsDirty(EParticleFlags::Dynamics))
			{
				FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame, CurWave);
				bChanged = LatestState.SyncDirtyDynamics(DestManager,DataIdx,Dirty.ParticleData,SrcManager);
			}

			if(bChanged)
			{
				//value actually changed, so we must desync
				Info.bDesync = true;
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

	void PushPTDirtyData(const TPBDRigidParticleHandle<FReal,3>& Rigid, const int32 DataIdxIn)
	{
		const int32 DataIdx = DataIdxIn + DataIdxOffset;

		//todo: is this check needed? why do we pass sleeping rigids into this function?
		if(SimWritablePropsMayChange(Rigid))
		{
			FDirtyParticleInfo& Info = FindOrAddParticle(*Rigid.GTGeometryParticle(), Rigid.UniqueIdx());
			Info.LastDirtyFrame = CurFrame-1;

			//User called PrepareManagerForFrame (or PrepareFrameForPTDirty) for the previous frame, so use it
			FDirtyPropertiesManager& DestManager = *Managers[CurFrame-1].Manager;

			//sim-writable properties changed at head, so we must write down what they were
			FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame-1, CurWave);
			bool bChanged = LatestState.SyncSimWritablePropsFromSim(DestManager,DataIdx,Rigid);
			if(bChanged)
			{
				Info.bDesync = true;
			}

			//update any previous frames that were pointing at head
			CoalesceBack(Info.Frames, CurFrame-1);
		}
	}

private:

	class FFrameInfo
	{
	public:
		FFrameInfo()
		: bSet(false)
		{

		}

		FGeometryParticleStateBase* GetState(int32 Frame, int8 Wave)
		{
			return (bSet && Frame == RecordedFrame && Wave == RewindWave) ? &State : nullptr;
		}

		const FGeometryParticleStateBase* GetState(int32 Frame, int8 Wave) const
		{
			return (bSet && Frame == RecordedFrame && Wave == RewindWave) ? &State : nullptr;
		}

		FGeometryParticleStateBase& GetStateChecked(int32 Frame, int8 Wave)
		{
			check(bSet && Frame == RecordedFrame && Wave == RewindWave);
			return State;
		}

		const FGeometryParticleStateBase& GetStateChecked(int32 Frame, int8 Wave) const
		{
			check(bSet && Frame == RecordedFrame && Wave == RewindWave);
			return State;
		}

		FGeometryParticleStateBase& NewState(int32 Frame, int8 Wave)
		{
			RecordedFrame = Frame;
			RewindWave = Wave;
			bSet = true;
			State = FGeometryParticleStateBase();
			return State;
		}

	private:
		FGeometryParticleStateBase State;
		int32 RecordedFrame;
		int8 RewindWave;
		bool bSet;
	};

	void CoalesceBack(TCircularBuffer<FFrameInfo>& Frames, int32 LatestIdx)
	{
		const FGeometryParticleStateBase& LatestState = Frames[LatestIdx].GetStateChecked(LatestIdx, CurWave);
		const int32 EarliestFrame = LatestIdx - FramesSaved;
		for(int32 FrameIdx = LatestIdx - 1; FrameIdx >= EarliestFrame; --FrameIdx)
		{
			if(FGeometryParticleStateBase* State = Frames[FrameIdx].GetState(FrameIdx, CurWave))
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

	const FGeometryParticleStateBase* GetStateAtFrameImp(const TGeometryParticle<FReal,3>& Particle,int32 Frame) const
	{
		if(const FDirtyParticleInfo* Info = FindParticle(Particle.UniqueIdx()))
		{
			const TCircularBuffer<FFrameInfo>& Frames = Info->Frames;
			if(const FGeometryParticleStateBase* FrameState = Frames[Frame].GetState(Frame, CurWave))
			{
				return FrameState;
			}

			//If frame is between two captures, use later capture. We always store the last data before a change
			//We can never use an earlier capture because the fact that we captured at all implies _something_ is different from proceeding frames

			for(int32 FrameIdx = Frame + 1; FrameIdx <= CurFrame; ++FrameIdx)
			{
				if(const FGeometryParticleStateBase* FrameState = Frames[FrameIdx].GetState(FrameIdx, CurWave))
				{
					return FrameState;
				}
			}
		}

		//If no data, or past capture, just use head
		return nullptr;
	}

	struct FDirtyParticleInfo
	{
		TCircularBuffer<FFrameInfo> Frames;
		TGeometryParticle<FReal,3>* Particle;
		FUniqueIdx CachedUniqueIdx;	//Needed when manipulating on physics thread and Particle data cannot be read
		int32 LastDirtyFrame;	//Track how recently this was made dirty
		bool bDesync;

		FDirtyParticleInfo(TGeometryParticle<FReal, 3>& UnsafeGTParticle, const FUniqueIdx UniqueIdx, const int32 CurFrame, const int32 NumFrames)
		: Frames(NumFrames)
		, Particle(&UnsafeGTParticle)
		, CachedUniqueIdx(UniqueIdx)
		, LastDirtyFrame(CurFrame)
		, bDesync(true)
		{

		}

		FGeometryParticleStateBase& AddFrame(int32 FrameIdx, int8 Wave)
		{
			FFrameInfo& Info = Frames[FrameIdx];
			if(FGeometryParticleStateBase* State = Info.GetState(FrameIdx, Wave))
			{
				return *State;
			}

			return Info.NewState(FrameIdx, Wave);
		}
	};

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
	int8 CurWave;
	int32 FramesSaved;
	int32 DataIdxOffset;
	bool bNeedsSave;	//Indicates that some data is pointing at head and requires saving before a rewind
};
}
