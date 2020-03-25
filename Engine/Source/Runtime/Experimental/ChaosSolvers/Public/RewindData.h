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

	void SyncSimWritablePropsFromSim(FDirtyPropertiesManager& Manager,int32 Idx,const TPBDRigidParticleHandle<FReal,3>& Rigid)
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
	, EarliestFrame(0)
	, bNeedsSave(false)
	{
	}

	uint32 Capacity() const { return Managers.Capacity(); }

	bool RewindToFrame(uint32 Frame)
	{
		ensure(IsInGameThread());

		//Can't go too far back, also we need 1 entry for saving head
		if(Frame < EarliestFrame)
		{
			return false;
		}

		//If we need to save and we are right on the edge of the buffer, we can't go back to earliest frame
		if(Frame == EarliestFrame && bNeedsSave && &Managers[CurFrame] == &Managers[EarliestFrame])
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
			if(bNeedsSave)
			{
				//GetStateAtFrameImp returns a pointer from the TArray that holds state data
				//But it's possible that we'll need to save state from head, which would grow that TArray
				//So preallocate just in case
				FParticleRewindInfo& RewindInfo = ParticleToRewindInfo.FindChecked(DirtyParticleInfo.CachedUniqueIdx);
				FGeometryParticleStateBase& LatestState = RewindInfo.AddFrame(CurFrame);
			
				if(const FGeometryParticleStateBase* RewindState = GetStateAtFrameImp(*DirtyParticleInfo.Particle, Frame))
				{
					LatestState.SyncIfDirty(*DestManager,DataIdx++,*DirtyParticleInfo.Particle,*RewindState);
					CoalesceBack(RewindInfo.Frames);

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
		EarliestFrame = CurFrame;	//can't rewind before this point. This simplifies saving the state at head

		return true;
	}

	void RemoveParticle(const TGeometryParticleHandle<FReal,3>& Particle)
	{
		if(const FParticleRewindInfo* Info = ParticleToRewindInfo.Find(Particle.UniqueIdx()))
		{
			const int32 Idx = Info->AllDirtyParticlesIdx;
			AllDirtyParticles.RemoveAtSwap(Idx);
			if(Idx< AllDirtyParticles.Num())
			{
				//update particle in new position
				ParticleToRewindInfo.FindChecked(AllDirtyParticles[Idx].CachedUniqueIdx).AllDirtyParticlesIdx = Idx;
			}

			ParticleToRewindInfo.RemoveChecked(Particle.UniqueIdx());
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
		++CurFrame;
		if(CurFrame > Managers.Capacity())
		{
			++EarliestFrame;
		}
	}

	void PrepareFrame(int32 NumDirtyParticles)
	{
		PrepareManagerForFrame(CurFrame,NumDirtyParticles);
	}

	int32 PrepareFrameForPTDirty(int32 NumActiveParticles)
	{
		bNeedsSave = true;

		//If manager already exists for previous frame, use it
		const int32 PrevFrame = CurFrame - 1;
		FFrameManagerInfo& Info = Managers[PrevFrame];
		ensure(Info.Manager && Info.FrameCreatedFor == (PrevFrame));

		{
			const int32 NumDirtyAlready = Info.Manager->GetNumParticles();
			Info.Manager->SetNumParticles(NumDirtyAlready + NumActiveParticles);
			return NumDirtyAlready;
		}
		return 0;
	}

	void PushGTDirtyData(const FDirtyPropertiesManager& SrcManager, int32 DataIdx, const FDirtyProxy& Dirty)
	{
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
			FParticleRewindInfo& Info = FindOrAddParticle(Proxy->GetParticle(),PTParticle->UniqueIdx());

			//Most properties are always a frame behind
			if(Proxy->IsInitialized())	//Frame delay so proxy must be initialized
			{
				FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame-1);
				LatestState.SyncPrevFrame(DestManager,DataIdx,Dirty);
				CoalesceBack(Info.Frames);
			}

			//If dynamics are dirty we must record them immediately because the sim resets them to 0
			if(Dirty.ParticleData.IsDirty(EParticleFlags::Dynamics))
			{
				FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame);
				LatestState.SyncDirtyDynamics(DestManager,DataIdx,Dirty.ParticleData,SrcManager);
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

	void PushPTDirtyData(const TPBDRigidParticleHandle<FReal,3>& Rigid, int32 DataIdx)
	{
		//todo: is this check needed? why do we pass sleeping rigids into this function?
		if(SimWritablePropsMayChange(Rigid))
		{
			FParticleRewindInfo& Info = FindOrAddParticle(Rigid.GTGeometryParticle(), Rigid.UniqueIdx());
			
			//User called PrepareManagerForFrame (or PrepareFrameForPTDirty) for the previous frame, so use it
			FDirtyPropertiesManager& DestManager = *Managers[CurFrame-1].Manager;

			//sim-writable properties changed at head, so we must write down what they were
			FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame-1);
			LatestState.SyncSimWritablePropsFromSim(DestManager,DataIdx,Rigid);

			//update any previous frames that were pointing at head
			CoalesceBack(Info.Frames);
		}
	}

private:

	struct FFrameInfo
	{
		FGeometryParticleStateBase State;
		int32 Frame;
	};

	struct FParticleRewindInfo
	{
		TArray<FFrameInfo> Frames;
		int32 AllDirtyParticlesIdx;

		FGeometryParticleStateBase& AddFrame(int32 FrameIdx)
		{
			if(Frames.Num() && Frames.Last().Frame == FrameIdx)
			{
				return Frames.Last().State;
			}

			Frames.Add(FFrameInfo{FGeometryParticleStateBase(),FrameIdx});
			return Frames.Last().State;
		}
	};

	void CoalesceBack(TArray<FFrameInfo>& Frames)
	{
		const FGeometryParticleStateBase& LatestState = Frames.Last().State;
		for(int32 FrameIdx = Frames.Num() - 2; FrameIdx >= 0; --FrameIdx)
		{
			FFrameInfo& Frame = Frames[FrameIdx];
			if(Frame.State.CoalesceState(LatestState) == false)
			{
				//nothing to coalesce so no need to check earlier frames
				break;
			}
		}
	}

	void PrepareManagerForFrame(int32 Frame, int32 NumDirtyParticles)
	{
		FFrameManagerInfo& Info = Managers[Frame];
		if(Info.Manager == nullptr)
		{
			Info.Manager = MakeUnique<FDirtyPropertiesManager>();
		}

		Info.Manager->SetNumParticles(NumDirtyParticles);
		Info.FrameCreatedFor = Frame;
	}

	struct FFrameManagerInfo
	{
		TUniquePtr<FDirtyPropertiesManager> Manager;

		//Note that this is not exactly the same as which frame this manager represents. 
		//A manager can have data for two frames at once, the important part is just knowing which frame it was created on so we know whether the physics data can rely on it
		//Consider the case where nothing is dirty from GT and then an object moves from the simulation, in that case it needs a manager to record the data into
		int32 FrameCreatedFor;
	};

	FParticleRewindInfo& FindOrAddParticle(TGeometryParticle<FReal,3>* UnsafeGTParticle, FUniqueIdx UniqueIdx)
	{
		if(FParticleRewindInfo* Info = ParticleToRewindInfo.Find(UniqueIdx))
		{
			return *Info;
		}


		FParticleRewindInfo& Info = ParticleToRewindInfo.FindOrAdd(UniqueIdx);
		Info.AllDirtyParticlesIdx = AllDirtyParticles.Add(FDirtyParticleInfo{UnsafeGTParticle,UniqueIdx});
		return Info;
	}

	const FGeometryParticleStateBase* GetStateAtFrameImp(const TGeometryParticle<FReal,3>& Particle,int32 Frame) const
	{
		if(const FParticleRewindInfo* Info = ParticleToRewindInfo.Find(Particle.UniqueIdx()))
		{
			//is it worth doing binary search?
			const int32 NumFrames = Info->Frames.Num();
			if(NumFrames > 0)
			{
				//If frame is before first capture, use first capture
				if(Frame <= Info->Frames[0].Frame)
				{
					return &Info->Frames[0].State;
				}

				//If frame is between two captures, use later capture, because we always store the last data before a change
				//We can never use an earlier capture, because the fact that we captured at all implies _something_ is different from proceeding frames
				for(int32 FrameIdx = 1; FrameIdx < NumFrames; ++FrameIdx)
				{
					if(Frame <= Info->Frames[FrameIdx].Frame)
					{
						return &Info->Frames[FrameIdx].State;
					}
				}
			}
		}

		//If no data, or past capture, just use head
		return nullptr;
	}

	struct FDirtyParticleInfo
	{
		TGeometryParticle<FReal,3>* Particle;
		FUniqueIdx CachedUniqueIdx;	//Needed when manipulating on physics thread and Particle data cannot be read
	};

	TArrayAsMap<FUniqueIdx,FParticleRewindInfo> ParticleToRewindInfo;
	TCircularBuffer<FFrameManagerInfo> Managers;
	TArray<FDirtyParticleInfo> AllDirtyParticles;
	uint32 CurFrame;
	uint32 EarliestFrame;
	bool bNeedsSave;	//Indicates that some data is pointing at head and requires saving before a rewind
};
}
