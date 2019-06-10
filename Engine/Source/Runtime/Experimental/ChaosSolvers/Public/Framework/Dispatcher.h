// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Threading.h"

class FChaosSolversModule;
class FPhysicsCommandsTask;

namespace Chaos
{
	class FPBDRigidsSolver;
	class FPersistentPhysicsTask;
}

namespace Chaos
{
	class IDispatcher
	{
	public:
		virtual ~IDispatcher() {}

		virtual void EnqueueCommand(TFunction<void()> InCommand) = 0;
		virtual void EnqueueCommand(TFunction<void(FPersistentPhysicsTask*)> InCommand) = 0;
		virtual void EnqueueCommand(FPBDRigidsSolver* InSolver, TFunction<void(FPBDRigidsSolver*)> InCommand) const = 0;
		virtual EThreadingMode GetMode() const = 0;
	};

	template<EThreadingMode Mode>
	class CHAOSSOLVERS_API FDispatcher : public IDispatcher
	{
	public:
		friend class FPersistentPhysicsTask;
		friend class ::FPhysicsCommandsTask;
		friend class FPhysScene_ChaosInterface;

		explicit FDispatcher(FChaosSolversModule* InOwnerModule)
			: Owner(InOwnerModule)
		{}

		virtual void EnqueueCommand(TFunction<void()> InCommand) final override;
		virtual void EnqueueCommand(TFunction<void(FPersistentPhysicsTask*)> InCommand) final override;
		virtual void EnqueueCommand(FPBDRigidsSolver* InSolver, TFunction<void(FPBDRigidsSolver*)> InCommand) const final override;
		virtual EThreadingMode GetMode() const final override { return Mode; }

	private:
		FChaosSolversModule* Owner;

		TQueue<TFunction<void()>, EQueueMode::Mpsc> GlobalCommandQueue;
		TQueue<TFunction<void(FPersistentPhysicsTask*)>, EQueueMode::Mpsc> TaskCommandQueue;
	};

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommand(TFunction<void()> InCommand);
	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommand(TFunction<void(FPersistentPhysicsTask*)> InCommand);
	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommand(FPBDRigidsSolver* InSolver, TFunction<void(FPBDRigidsSolver *)> InCommand) const;

	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommand(TFunction<void()> InCommand);
	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommand(TFunction<void(FPersistentPhysicsTask*)> InCommand);
	template<>
	void FDispatcher<EThreadingMode::SingleThread>::EnqueueCommand(FPBDRigidsSolver* InSolver, TFunction<void(FPBDRigidsSolver *)> InCommand) const;

	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommand(TFunction<void()> InCommand);
	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommand(TFunction<void(FPersistentPhysicsTask*)> InCommand);
	template<>
	void FDispatcher<EThreadingMode::DedicatedThread>::EnqueueCommand(FPBDRigidsSolver* InSolver, TFunction<void(FPBDRigidsSolver *)> InCommand) const;
}

void LexFromString(Chaos::EThreadingMode& OutValue, const TCHAR* InString);
FString LexToString(const Chaos::EThreadingMode InValue); 

#endif
