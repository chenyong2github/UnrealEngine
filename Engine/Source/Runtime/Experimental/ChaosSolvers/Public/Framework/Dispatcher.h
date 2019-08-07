// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Threading.h"
#include "Chaos/Declares.h"

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
	/**
	 * Physics command dispatcher
	 *
	 * Used to safely interact with Chaos physics data. When performing any operation that needs to affect live
	 * physics data such as modifying simulation data (positions, velocities etc.) make sure that this code
	 * is placed with in an appropriate command.
	 *
	 * Command Types:
	 *
	 *		* Global -  First commands to run in a batch, no parameters, if your command doesn't interact
	 *					with a solver, this is probably what you need. This will run at the beginning of
	 *					a physics tick before task or solver commands
	 *
	 *		* Task -	Ran immediately after the global commands, takes a ptr to the actual physics task
	 *					that is running if we are in dedicated thread mode, nullptr otherwise. Although this
	 *					command type knows about the underlying threading model care should be taken when using
	 *					this command type to make sure it also accomplishes its goal in non-dedicated threading modes.
	 *					Only use if absolutely necessary
	 *
	 *		* Solver -	Another useful command, is bound to a solver. Runs after the task commands have all run.
	 *					Maybe all be ran in different threads. The solver pointer passed to the command is safe
	 *					to read and write from (as should be any captured object handles) but do not attempt to
	 *					access other solvers, or other off-thread data as this will not be safe when running in
	 *					any threading model that is not single-threaded
	 *
	 * A note on batched commands and command lists:
	 *		Batched commands and command lists are consistent with the above ordering of commands but only within
	 *		that specific batch. If two batched A and B are submitted then the execution order of the commands is:
	 *				A.Global -> A.Task -> A.Solver -> B.Global -> B.Task -> B.Solver
	 */
	class IDispatcher
	{
	public:
		virtual ~IDispatcher() {}

		using FGlobalCommand = TFunction<void()>;
		using FTaskCommand = TFunction<void(FPersistentPhysicsTask*)>;
		using FSolverCommand = TFunction<void(FPhysicsSolver*)>;

		/**
		 * Immediate commands:
		 * Enqueueing an immediate command will run that command at the next available opportunity when
		 * the physics scene is next ticked. Note that in some threading models commands enqueued immediately
		 * after one another on a different thread may get executed in different physics frames.
		 * If this is not desirable consider either batched commands or submitting a custom command list
		 */
		virtual void EnqueueCommandImmediate(FGlobalCommand InCommand) = 0;
		virtual void EnqueueCommandImmediate(FTaskCommand InCommand) = 0;
		virtual void EnqueueCommandImmediate(FPhysicsSolver* InSolver, FSolverCommand InCommand) = 0;

		/**
		 * Get the current threading mode for this dispatcher
		 */
		virtual EThreadingMode GetMode() const = 0;

		/**
		 * Given a command list, submit it as a batch for execution on the next physics frame
		 */
		virtual void SubmitCommandList(TUniquePtr<FCommandListData>&& InCommandData) = 0;

		/**
		 * Execute any pending submitted command lists along with global and task commands
		 * Intended to be called by whatever code is responsible for updating the physics scene
		 * #BG - Make solver commands execute here too instead of 2 execution sites (Needs parallel consideration).
		 */
		virtual void Execute() = 0;
	};

}

void LexFromString(Chaos::EThreadingMode& OutValue, const TCHAR* InString);
FString LexToString(const Chaos::EThreadingMode InValue); 

#endif
