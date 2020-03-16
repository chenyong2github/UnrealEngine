// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Core.h"
#include "Chaos/ParticleHandle.h"

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

	TParticleStateProperty(const FDirtyPropertiesManager* InManager, int32 InIdx)
	: Manager(InManager)
	, Idx(InIdx)
	{
	}

	template <typename LambdaRead>
	const T& Read(const LambdaRead& ReadFunc) const
	{
		if(Manager)
		{
			const TDirtyElementPool<T>& Pool = Manager->GetParticlePool<T,PropName>();
			return Pool.GetElement(Idx);
		}
		else
		{
			return ReadFunc();
		}
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
			return ParticlePositionRotation.Read([this]() -> const auto& {return Particle->XR();}).X;
		}

	protected:
		TGeometryParticle<FReal,3>* Particle;
	private:
		TParticleStateProperty<FParticlePositionRotation,EParticleProperty::XR> ParticlePositionRotation;
	};

	class FRewindData
	{
	public:
		FRewindData(int32 NumFrames)
			: CurFrame(0)
		{
		}

		const FGeometryParticleState* GetStateAtFrame(const TGeometryParticle<FReal,3>& Particle, int32 Frame) const
		{
			if(const TArray<FFrameInfo>* Frames = ParticleToFrameInfo.Find(Particle))
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

		void SetSecondLastState(const FDirtyProxy& DirtyProxy)
		{
			//NOTE: this is called from PT so we cannot use the GTParticle to read
			//Instead we  must use the proxy system
			//However, the rewind happens on GT so that's why we still want to associate this data with the GTParticle

			auto ProcessProxy = [this](const auto Proxy)
			{
				const auto GTParticleUnsafe = Proxy->GetParticle();
				TArray<FGeometryParticleState>& Frames = ParticleToFrameInfo.FindOrAdd(*GTParticleUnsafe);
				Frames.Add({FGeometryParticleState(GTParticleUnsafe),CurFrame});
			};

			switch(DirtyProxy.Proxy->GetType())
			{
			case EPhysicsProxyType::SingleGeometryParticleType: ProcessProxy(static_cast<const FGeometryParticlePhysicsProxy*>(DirtyProxy.Proxy)); break;
			case EPhysicsProxyType::SingleKinematicParticleType: ProcessProxy(static_cast<const FKinematicGeometryParticlePhysicsProxy*>(DirtyProxy.Proxy)); break;
			case EPhysicsProxyType::SingleRigidParticleType: ProcessProxy(static_cast<const FRigidParticlePhysicsProxy*>(DirtyProxy.Proxy)); break;
			default: check(false);
			}
		}

		void AdvanceFrame()
		{
			++CurFrame
		}
		
	private:

		struct FFrameInfo
		{
			FGeometryParticleState State;
			int32 Frame;
		};

		TArrayAsMap<TGeometryParticle<FReal,3>, TArray<FFrameInfo>> ParticleToFrameInfo;
		int32 CurFrame;
	};
}
