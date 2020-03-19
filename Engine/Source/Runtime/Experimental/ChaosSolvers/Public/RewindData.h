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

//Set of properties that can be written to from the simulation
class FSimWritableData
{
public:

	FSimWritableData()
	{
	}

	const FParticlePositionRotation& GetXR() const { return XR; }
	const FParticleVelocities& GetVelocities() const { return Velocities; }
	const FParticleDynamics& GetDynamics() const { return Dynamics; }

	void SyncData(const FDirtyPropertiesManager& Manager, const int32 DataIdx, const FParticleDirtyData& Dirty)
	{
		if(const auto Data = Dirty.FindXR(Manager, DataIdx))
		{
			XR = *Data;
		}

		if(const auto Data = Dirty.FindVelocities(Manager, DataIdx))
		{
			Velocities = *Data;
		}

		if(const auto Data = Dirty.FindDynamics(Manager, DataIdx))
		{
			Dynamics = *Data;
		}
	}

private:
	FParticlePositionRotation XR;
	FParticleVelocities Velocities;
	FParticleDynamics Dynamics;
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

	void SyncRemoteData(FDirtyPropertiesManager& Manager,int32 Idx,const FDirtyProxy& Dirty, const FSimWritableData* SimWritableData)
	{
		const auto Proxy = static_cast<const FGeometryParticlePhysicsProxy*>(Dirty.Proxy);
		const auto Handle = Proxy->GetHandle();
		ParticlePositionRotation.SyncRemoteData(Manager,Idx,Dirty.ParticleData, [Handle, SimWritableData](FParticlePositionRotation& Data)
		{
			if(SimWritableData)
			{
				Data = SimWritableData->GetXR();
			}
			else
			{
				Data.X = Handle->X();
				Data.R = Handle->R();
			}
		});

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

		if(auto PBDRigid = Handle->CastToRigidParticle())
		{
			Dynamics.SyncRemoteData(Manager,Idx,Dirty.ParticleData,[PBDRigid, SimWritableData](FParticleDynamics& Data)
			{
				if(SimWritableData)
				{
					Data = SimWritableData->GetDynamics();
				}
				else
				{
					Data.F = PBDRigid->F();
					Data.Torque = PBDRigid->Torque();
					Data.LinearImpulse = PBDRigid->LinearImpulse();
					Data.AngularImpulse = PBDRigid->AngularImpulse();
					Data.LinearEtherDrag = PBDRigid->LinearEtherDrag();
					Data.AngularEtherDrag = PBDRigid->AngularEtherDrag();
				}
			});
		}
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

		if(!Dynamics.IsSet() && LatestState.Dynamics.IsSet())
		{
			Dynamics = LatestState.Dynamics;
			bCoalesced = true;
		}

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
			for(const FFrameInfo& FrameInfo : Info->Frames)
			{
				if(FrameInfo.Frame == Frame)
				{
					return &FrameInfo.State;
				}
			}
		}

		return nullptr;
	}

	void AdvanceFrame()
	{
		++CurFrame;
	}

	void PrepareFrame(int32 NumDirtyParticles)
	{
		Managers.Emplace(MakeUnique<FDirtyPropertiesManager>());
		Managers.Last()->SetNumParticles(NumDirtyParticles);
	}

	void PushGTDirtyData(const FDirtyPropertiesManager& Manager, int32 DataIdx, const FDirtyProxy& Dirty)
	{
		auto ProcessProxy = [this,Manager, DataIdx, Dirty](const auto Proxy)
		{
			const auto PTParticle = Proxy->GetHandle();
			FParticleRewindInfo& Info = ParticleToRewindInfo.FindOrAdd(PTParticle->UniqueIdx());

			//Any previous frames that are pointing at head of a dirty property must be written out (since head is changing)

			//Since we are updating previous frames, the proxy must have been initialized already
			if(Proxy->IsInitialized())
			{
				FGeometryParticleState& LatestState = Info.AddFrame(*Proxy->GetParticle(),CurFrame-1);

				LatestState.SyncRemoteData(*Managers.Last(),DataIdx,Dirty, Info.SimWritableData.Get());

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

			//if particle is sim-writable make sure to record appropriate dirty properties
			if(SimWritablePropsMayChange(*PTParticle))
			{
				if(!Info.SimWritableData)
				{
					Info.SimWritableData = MakeUnique<FSimWritableData>();
				}
				Info.SimWritableData->SyncData(Manager,DataIdx,Dirty.ParticleData);
			}
			else
			{
				Info.SimWritableData = nullptr;
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

private:

	struct FFrameInfo
	{
		FGeometryParticleState State;
		int32 Frame;
	};

	struct FParticleRewindInfo
	{
		TArray<FFrameInfo> Frames;
		TUniquePtr<FSimWritableData> SimWritableData;

		FGeometryParticleState& AddFrame(TGeometryParticle<FReal,3>& GTParticleUnsafe, int32 FrameIdx)
		{
			Frames.Add(FFrameInfo{FGeometryParticleState(GTParticleUnsafe),FrameIdx});
			return Frames.Last().State;
		}
	};

	TArrayAsMap<FUniqueIdx,FParticleRewindInfo> ParticleToRewindInfo;
	TArray<TUniquePtr<FDirtyPropertiesManager>> Managers;
	int32 CurFrame;
};
}
