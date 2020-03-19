// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Core.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "Chaos/Framework/PhysicsSolverBase.h"

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
	void SyncToParticle(LambdaWrite& WriteFunc) const
	{
		if(Manager)
		{
			const TDirtyElementPool<T>& Pool = Manager->GetParticlePool<T,PropName>();
			const T& Value = Pool.GetElement(Idx);
			WriteFunc(Value);
		}
	}

	template <typename LambdaSet>
	void SyncRemoteData(FDirtyPropertiesManager& InManager,int32 InIdx, const FParticleDirtyData& DirtyData, const LambdaSet& SetFunc)
	{
		if(DirtyData.IsDirty(ParticlePropToFlag(PropName)))
		{
			Manager = &InManager;
			Idx = InIdx;
			SetFunc(Manager->GetParticlePool<T,PropName>().GetElement(Idx));
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

class FGeometryParticleState
{
public:

	FGeometryParticleState(const TGeometryParticle<FReal,3>& InParticle)
		: Particle(InParticle)
	{
	}

	const FVec3& X() const
	{
		return ParticlePositionRotation.IsSet() ? ParticlePositionRotation.Read().X : Particle.X();
	}

	TSerializablePtr<FImplicitObject> Geometry() const
	{
		return NonFrequentData.IsSet() ? MakeSerializable(NonFrequentData.Read().Geometry) : Particle.Geometry();
	}

	const FVec3& F() const
	{
		return Dynamics.IsSet() ? Dynamics.Read().F : Particle.CastToRigidParticle()->F();
	}

	void SyncSimWritablePropsFromSim(FDirtyPropertiesManager& Manager,int32 Idx, const TPBDRigidParticleHandle<FReal,3>& Rigid)
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

		//todo: velocities
	}

	void SyncDirtyDynamics(FDirtyPropertiesManager& DestManager,int32 DataIdx, const FParticleDirtyData& Dirty, const FDirtyPropertiesManager& SrcManager)
	{
		FParticleDirtyData DirtyFlags;
		DirtyFlags.SetFlags(Dirty.GetFlags());

		Dynamics.SyncRemoteData(DestManager,DataIdx,DirtyFlags,[&Dirty, &SrcManager, DataIdx](auto& Data)
		{
			Data = Dirty.GetDynamics(SrcManager, DataIdx);
		});
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
		}
		
		NonFrequentData.SyncRemoteData(Manager,Idx,Dirty.ParticleData, [Handle](FParticleNonFrequentData& Data)
		{
			Data.Geometry = Handle->SharedGeometryLowLevel();
			Data.UserData = Handle->UserData();

			//note: this data is keyed based on unique idx so it's not really possible to change this
			//but we save it anyway since it's part of a big struct
			Data.UniqueIdx = Handle->UniqueIdx();
#if CHAOS_CHECKED
			Data.DebugName = Handle->DebugName();
#endif
		});
	}

	bool CoalesceState(const FGeometryParticleState& LatestState)
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

		//dynamics do not coalesce since they are always written when dirty
		
		return bCoalesced;
	}

protected:
	const TGeometryParticle<FReal,3>& Particle;
private:
	TParticleStateProperty<FParticlePositionRotation,EParticleProperty::XR> ParticlePositionRotation;
	TParticleStateProperty<FParticleNonFrequentData, EParticleProperty::NonFrequentData> NonFrequentData;
	TParticleStateProperty<FParticleDynamics, EParticleProperty::Dynamics> Dynamics;
	/*
	PARTICLE_PROPERTY(XR,FParticlePositionRotation)
		PARTICLE_PROPERTY(Velocities,FParticleVelocities)
		PARTICLE_PROPERTY(Dynamics,FParticleDynamics)
		PARTICLE_PROPERTY(Misc,FParticleMisc)
		PARTICLE_PROPERTY(NonFrequentData,FParticleNonFrequentData)
		PARTICLE_PROPERTY(MassProps,FParticleMassProps)*/
};

class FRewindData
{
public:
	FRewindData(int32 NumFrames)
		: CurFrame(0)
	{
	}

	const FGeometryParticleState* GetStateAtFrame(const TGeometryParticle<FReal,3>& Particle,int32 Frame) const
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

	void AdvanceFrame()
	{
		++CurFrame;
	}

	void PrepareFrame(int32 NumDirtyParticles)
	{
		Managers.Emplace(FFrameManagerInfo{MakeUnique<FDirtyPropertiesManager>(), CurFrame});
		Managers.Last().Manager->SetNumParticles(NumDirtyParticles);
	}

	int32 PrepareFrameForPTDirty(int32 NumActiveParticles)
	{
		//If manager already exists for previous frame, use it
		if(Managers.Num())
		{
			if(Managers.Last().FrameCreatedFor == (CurFrame-1))
			{
				FDirtyPropertiesManager& Manager = *Managers.Last().Manager;
				const int32 NumDirtyAlready = Manager.GetNumParticles();
				Manager.SetNumParticles(NumDirtyAlready + NumActiveParticles);
				return NumDirtyAlready;
			}
		}

		//No manager for previous frame so create a new one
		PrepareFrame(NumActiveParticles);
		return 0;
	}

	void PushGTDirtyData(const FDirtyPropertiesManager& SrcManager, int32 DataIdx, const FDirtyProxy& Dirty)
	{
		//This records changes enqueued by GT.
		//Most new particles do not change, so to avoid useless writes we wait until the next frame's dirty flag
		//This is possible because most properties are const on the physics thread
		//For sim-writable properties (forces, position, velocities, etc...) we must immediately write the data because there is no way to know what the previous data was next frame
		//Some sim-writable properties can change without the GT knowing about it, see PushPTDirtyData
		
		auto ProcessProxy = [this,&SrcManager, DataIdx, Dirty](const auto Proxy)
		{
			const auto PTParticle = Proxy->GetHandle();
			FParticleRewindInfo& Info = ParticleToRewindInfo.FindOrAdd(PTParticle->UniqueIdx());
			FDirtyPropertiesManager& DestManager = *Managers.Last().Manager;

			//Most properties are always a frame behind
			if(Proxy->IsInitialized())	//Frame delay so proxy must be initialized
			{
				FGeometryParticleState& LatestState = Info.AddFrame(*Proxy->GetParticle(),CurFrame-1);
				LatestState.SyncPrevFrame(DestManager,DataIdx,Dirty);

				//for frames further back a simply copy is enough
				for(int32 FrameIdx = Info.Frames.Num() - 2; FrameIdx >= 0; --FrameIdx)
				{
					FFrameInfo& Frame = Info.Frames[FrameIdx];
					if(Frame.State.CoalesceState(LatestState) == false)
					{
						//nothing to coalesce so no need to check earlier frames
						break;
					}
				}
			}

			//If dynamics are dirty we must record them immediately because the sim resets them to 0
			if(Dirty.ParticleData.IsDirty(EParticleFlags::Dynamics))
			{
				FGeometryParticleState& LatestState = Info.AddFrame(*Proxy->GetParticle(),CurFrame);
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
			FParticleRewindInfo& Info = ParticleToRewindInfo.FindOrAdd(Rigid.UniqueIdx());
			FDirtyPropertiesManager& Manager = *Managers.Last().Manager;

			//sim-writable properties changed at head, so we must write down what they were
			FGeometryParticleState& LatestState = Info.AddFrame(*Rigid.GTGeometryParticle(),CurFrame-1);
			LatestState.SyncSimWritablePropsFromSim(Manager,DataIdx,Rigid);

			//update any previous frames that were pointing at head
			CoalesceBack(Info.Frames);
		}
	}

private:

	struct FFrameInfo
	{
		FGeometryParticleState State;
		int32 Frame;
	};

	struct FParticleRewindInfo
	{
		TArray<FFrameInfo> Frames;

		FGeometryParticleState& AddFrame(TGeometryParticle<FReal,3>& GTParticleUnsafe, int32 FrameIdx)
		{
			if(Frames.Num() && Frames.Last().Frame == FrameIdx)
			{
				return Frames.Last().State;
			}

			Frames.Add(FFrameInfo{FGeometryParticleState(GTParticleUnsafe),FrameIdx});
			return Frames.Last().State;
		}
	};

	void CoalesceBack(TArray<FFrameInfo>& Frames)
	{
		const FGeometryParticleState& LatestState = Frames.Last().State;
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

	struct FFrameManagerInfo
	{
		TUniquePtr<FDirtyPropertiesManager> Manager;

		//Note that this is not exactly the same as which frame this manager represents. 
		//A manager can have data for two frames at once, the important part is just knowing which frame it was created on so we know whether the physics data can rely on it
		//Consider the case where nothing is dirty from GT and then an object moves from the simulation, in that case it needs a manager to record the data into
		int32 FrameCreatedFor;
	};

	TArrayAsMap<FUniqueIdx,FParticleRewindInfo> ParticleToRewindInfo;
	TArray<FFrameManagerInfo> Managers;
	int32 CurFrame;
};
}
