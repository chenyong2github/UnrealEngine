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
	void SyncRemoteData(FDirtyPropertiesManager* InManager,int32 InIdx, const FParticleDirtyData& DirtyData, const LambdaSet& SetFunc)
	{
		if(DirtyData.IsDirty(ParticlePropToFlag(PropName)))
		{
			Manager = InManager;
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

class FGeometryParticleState
{
public:

	FGeometryParticleState(TGeometryParticle<FReal,3>* InParticle)
		: Particle(InParticle)
	{
	}

	const FVec3& X() const
	{
		return ParticlePositionRotation.IsSet() ? ParticlePositionRotation.Read().X : Particle->X();
	}

	TSerializablePtr<FImplicitObject> Geometry() const
	{
		return NonFrequentData.IsSet() ? MakeSerializable(NonFrequentData.Read().Geometry) : Particle->Geometry();
	}

	void SyncRemoteData(FDirtyPropertiesManager* Manager,int32 Idx,const FDirtyProxy& Dirty)
	{
		const auto Proxy = static_cast<const FGeometryParticlePhysicsProxy*>(Dirty.Proxy);
		const auto Handle = Proxy->GetHandle();
		ParticlePositionRotation.SyncRemoteData(Manager,Idx,Dirty.ParticleData, [Handle](FParticlePositionRotation& Data)
		{
			Data.X = Handle->X();
			Data.R = Handle->R();
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

		return bCoalesced;
	}

protected:
	TGeometryParticle<FReal,3>* Particle;
private:
	TParticleStateProperty<FParticlePositionRotation,EParticleProperty::XR> ParticlePositionRotation;
	TParticleStateProperty<FParticleNonFrequentData, EParticleProperty::NonFrequentData> NonFrequentData;
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
		if(const TArray<FFrameInfo>* Frames = ParticleToFrameInfo.Find(Particle.UniqueIdx()))
		{
			//is it worth doing binary search?
			for(const FFrameInfo& Info : (*Frames))
			{
				if(Info.Frame == Frame)
				{
					return &Info.State;
				}
			}
		}

		return nullptr;
	}

	void SavePrevFrameState(const FDirtySet& DirtyData)
	{
		Managers.Emplace(MakeUnique<FDirtyPropertiesManager>());

		FDirtyPropertiesManager* Manager = Managers.Last().Get();
		Manager->SetNumParticles(DirtyData.NumDirtyProxies());

		//NOTE: this is called from PT so we cannot use the GTParticle to read
		//Instead we  must use the proxy system
		//However, the rewind happens on GT so that's why we still want to associate this data with the GTParticle

		auto ProcessProxy = [this,Manager](const auto Proxy,const int32 DataIdx,const FDirtyProxy& Dirty)
		{
			//Since we're saving last frame's data the proxy must already be initialized
			if(Proxy->IsInitialized())
			{
				const auto GTParticleUnsafe = Proxy->GetParticle();
				const auto PTParticle = Proxy->GetHandle();
				TArray<FFrameInfo>& Frames = ParticleToFrameInfo.FindOrAdd(PTParticle->UniqueIdx());
				Frames.Add(FFrameInfo{FGeometryParticleState(GTParticleUnsafe),CurFrame-1});

				FGeometryParticleState& LatestState = Frames.Last().State;
				LatestState.SyncRemoteData(Manager,DataIdx,Dirty);

				//coalesce any previous data
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
		};

		//can't be parallel because ParticletoFrameInfo is modified
		DirtyData.ForEachProxy([&ProcessProxy](int32 DataIdx,const FDirtyProxy& Dirty)
		{
			switch(Dirty.Proxy->GetType())
			{
			case EPhysicsProxyType::SingleRigidParticleType:
			{
				auto Proxy = static_cast<FRigidParticlePhysicsProxy*>(Dirty.Proxy);
				ProcessProxy(Proxy,DataIdx,Dirty);
				break;
			}
			case EPhysicsProxyType::SingleKinematicParticleType:
			{
				auto Proxy = static_cast<FKinematicGeometryParticlePhysicsProxy*>(Dirty.Proxy);
				ProcessProxy(Proxy,DataIdx,Dirty);
				break;
			}
			case EPhysicsProxyType::SingleGeometryParticleType:
			{
				auto Proxy = static_cast<FGeometryParticlePhysicsProxy*>(Dirty.Proxy);
				ProcessProxy(Proxy,DataIdx,Dirty);
				break;
			}
			default:
			ensure("Unknown proxy type in physics solver.");
			}
		});

		++CurFrame;
	}

private:

	struct FFrameInfo
	{
		FGeometryParticleState State;
		int32 Frame;
	};

	TArrayAsMap<FUniqueIdx,TArray<FFrameInfo>> ParticleToFrameInfo;
	TArray<TUniquePtr<FDirtyPropertiesManager>> Managers;
	int32 CurFrame;
};
}
