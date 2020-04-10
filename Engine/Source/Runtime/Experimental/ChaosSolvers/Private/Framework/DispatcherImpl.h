// Copyright Epic Games, Inc. All Rights Reserved.

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

		FCriticalSection ConsumerLock;
	};

	////////////////////////////////////////////////////////////////////////////

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommandImmediate(FPhysicsSolver* InSolver, FSolverCommand InCommand);

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommandImmediate(FTaskCommand InCommand);

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommandImmediate(FGlobalCommand InCommand);

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::SubmitCommandList(TUniquePtr<FCommandListData>&& InCommandData);

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::Execute();

	////////////////////////////////////////////////////////////////////////////

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommandImmediate(FPhysicsSolver* InSolver, FSolverCommand InCommand);

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommandImmediate(FTaskCommand InCommand);

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommandImmediate(FGlobalCommand InCommand);

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::SubmitCommandList(TUniquePtr<FCommandListData>&& InCommandData);

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::Execute();

	////////////////////////////////////////////////////////////////////////////

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommandImmediate(FPhysicsSolver* InSolver, FSolverCommand InCommand);

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommandImmediate(FTaskCommand InCommand);

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::EnqueueCommandImmediate(FGlobalCommand InCommand);

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::SubmitCommandList(TUniquePtr<FCommandListData>&& InCommandData);

	template<>
	void FDispatcher<EThreadingMode::TaskGraph>::Execute();

	//////////////////////////////////////////////////////////////////////////

	extern template class FDispatcher<EThreadingMode::DedicatedThread>;
	extern template class FDispatcher<EThreadingMode::SingleThread>;
	extern template class FDispatcher<EThreadingMode::TaskGraph>;
}
