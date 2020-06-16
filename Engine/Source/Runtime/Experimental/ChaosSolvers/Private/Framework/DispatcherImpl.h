// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Framework/Threading.h"
#include "Chaos/Declares.h"

class FChaosSolversModule;
class FPhysicsCommandsTask;

#if 0

namespace Chaos
{
	class FPersistentPhysicsTask;
}

namespace Chaos
{
	template<EThreadingMode Mode>
	class FDispatcher : public IDispatcher
	{
	public:
		friend class FPersistentPhysicsTask;
		friend class ::FPhysicsCommandsTask;
		friend class FPhysScene_ChaosInterface;

		FDispatcher() = delete;
		FDispatcher(const FDispatcher& InCopy) = delete;
		FDispatcher(FDispatcher&& InSteal) = delete;
		FDispatcher& operator=(const FDispatcher& InCopy) = delete;
		FDispatcher& operator=(FDispatcher&& InSteal) = delete;

		explicit FDispatcher(FChaosSolversModule* InOwnerModule)
			: Owner(InOwnerModule)
		{
		}


		virtual EThreadingMode GetMode() const final override { return Mode; }

	private:

		FChaosSolversModule* Owner;

		FCriticalSection ConsumerLock;
	};

	extern template class FDispatcher<EThreadingMode::DedicatedThread>;
	extern template class FDispatcher<EThreadingMode::SingleThread>;
	extern template class FDispatcher<EThreadingMode::TaskGraph>;
}

#endif