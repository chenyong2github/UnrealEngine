// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Matrix.h"
#include "Misc/ScopeLock.h"

class IPhysicsProxyBase; // WTF - not in Chaos?

namespace Chaos
{

	class CHAOS_API FPhysicsSolverBase
	{
	public:

		FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn)
			: BufferMode(BufferingModeIn)
		{}
		
		void ChangeBufferMode(EMultiBufferMode InBufferMode);

		void AddDirtyProxy(IPhysicsProxyBase * ProxyBaseIn)
		{
			FScopeLock Lock(&DirtyProxiesCriticalSection);
			DirtyProxiesSet.Add(ProxyBaseIn);
		}
		void RemoveDirtyProxy(IPhysicsProxyBase * ProxyBaseIn)
		{
			FScopeLock Lock(&DirtyProxiesCriticalSection);
			DirtyProxiesSet.Remove(ProxyBaseIn);
		}

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
			FCriticalSection DirtyProxiesCriticalSection;
			TSet< IPhysicsProxyBase *> DirtyProxiesSet;

#if CHAOS_CHECKED
			FName DebugName;
#endif
	};
}
