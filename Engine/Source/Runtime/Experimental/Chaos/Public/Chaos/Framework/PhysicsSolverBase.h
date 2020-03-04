// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Matrix.h"
#include "Misc/ScopeLock.h"

class IPhysicsProxyBase; // WTF - not in Chaos?

namespace Chaos
{
	class FPBDRigidsEvolutionGBF;
	using FPBDRigidsEvolution = FPBDRigidsEvolutionGBF;

	class CHAOS_API FPhysicsSolverBase
	{
	public:

		FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn);
		~FPhysicsSolverBase();
		FPhysicsSolverBase(const FPhysicsSolverBase&) = delete;
		FPhysicsSolverBase(FPhysicsSolverBase&&);
		
		void ChangeBufferMode(EMultiBufferMode InBufferMode);

		void AddDirtyProxy(IPhysicsProxyBase * ProxyBaseIn)
		{
			DirtyProxiesSet.Add(ProxyBaseIn);
		}
		void RemoveDirtyProxy(IPhysicsProxyBase * ProxyBaseIn)
		{
			DirtyProxiesSet.Remove(ProxyBaseIn);
		}

		FPBDRigidsEvolution* GetEvolution() { return MEvolution.Get(); }
		FPBDRigidsEvolution* GetEvolution() const { return MEvolution.Get(); }

#if CHAOS_CHECKED
		void SetDebugName(const FName& Name)
		{
			DebugName = Name;
		}

		const FName& GetDebugName() const
		{
			return DebugName;
		}
#endif

	protected:
			EMultiBufferMode BufferMode;

			// Input Proxy Map
			TSet< IPhysicsProxyBase *> DirtyProxiesSet;

#if CHAOS_CHECKED
			FName DebugName;
#endif
			//TODO: Make more extensible
			TUniquePtr<FPBDRigidsEvolution> MEvolution;

			void SetEvolution(TUniquePtr<FPBDRigidsEvolution>&& Evolution);
	};
}
