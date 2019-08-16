// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Framework/Threading.h"
#include "Chaos/Declares.h"
#include "Framework/CommandBuffer.h"

class FChaosSolversModule;
class FPhysicsCommandsTask;

namespace Chaos
{
	class FPersistentPhysicsTask;
	class FCommandList;
	class FCommandListData;
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

		virtual void EnqueueCommandImmediate(FGlobalCommand InCommand) final override;
		virtual void EnqueueCommandImmediate(FTaskCommand InCommand) final override;
		virtual void EnqueueCommandImmediate(FPhysicsSolver* InSolver, FSolverCommand InCommand) final override;

		virtual EThreadingMode GetMode() const final override { return Mode; }

		virtual void SubmitCommandList(TUniquePtr<FCommandListData>&& InCommandData) override;

		virtual void Execute() override;

	private:

		FChaosSolversModule* Owner;

		TQueue<TFunction<void()>, EQueueMode::Mpsc> GlobalCommandQueue;
		TQueue<TFunction<void(FPersistentPhysicsTask*)>, EQueueMode::Mpsc> TaskCommandQueue;
		TQueue<TUniquePtr<FCommandListData>, EQueueMode::Mpsc> CommandLists;
	};

	extern template class FDispatcher<EThreadingMode::DedicatedThread>;
	extern template class FDispatcher<EThreadingMode::SingleThread>;
	extern template class FDispatcher<EThreadingMode::TaskGraph>;
}

#endif