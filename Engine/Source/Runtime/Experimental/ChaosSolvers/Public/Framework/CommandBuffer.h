// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dispatcher.h"
#include "Threading.h"

namespace Chaos
{
	/**
	 * \class Chaos::FCommandListData
	 * \brief Command list internal data
	 *
	 * Command list internal data, private data for a command list that is submitted to
	 * the physics dispatcher when the owning FCommandList is flushed (See FCommandList below)
	 */
	class FCommandListData
	{
	public:

		friend class FCommandList;

		template<Chaos::EThreadingMode>
		friend class FDispatcher;
		
		// Element type for solver command storage
		using FSolverCommandTuple = TTuple<FPhysicsSolver*, IDispatcher::FSolverCommand>;

		// Whether we have any pending commands waiting to submit
		bool HasCommands() const;

		// Can't copy or directly move list data (command list hands over ptrs to list data)
		FCommandListData(const FCommandListData& InCopy) = delete;
		FCommandListData(FCommandListData&& InSteal) = delete;
		FCommandListData& operator=(const FCommandListData& InCopy) = delete;
		FCommandListData& operator=(FCommandListData&& InSteal) = delete;

	private:

		// Only friends can create a data entry - only intended to be used by command lists
		FCommandListData() = default;

		// Storage for pending commands
		TArray<IDispatcher::FGlobalCommand> GlobalCommands;
		TArray<IDispatcher::FTaskCommand> TaskCommands;
		TArray<FSolverCommandTuple> SolverCommands;
	};

	/**
	 * \class Chaos::FCommandList
	 * \brief Physics command list
	 *
	 * When performing complex interactions with the physics engine it may be desirable
	 * to have a batch of commands in a way that ensures all commands are executed together
	 * before a physics tick. For this case use a command list, enqueueing all commands to the
	 * list and calling flush when finished.
	 *
	 * If you have an object that does this often just keep an FCommandList as a member as each
	 * call to flush will submit the commands to the physics dispatcher and prepare for a new
	 * batch, for repeated use this is better than creating a temporary to submit commands.
	 *
	 * Example usage:
	 * FCommandList MyList;
	 * MyList.Enqueue([](){
	 *     // Do command body 1
	 * });
	 * MyList.Enqueue([](){
	 *     // Do command body 2
	 * });
	 * MyList.Enqueue([](){
	 *     // Do command body 3
	 * });
	 * MyList.Flush();
	 */
	class FCommandList
	{
	public:

		friend class FChaosSolversModule;

		FCommandList();
		FCommandList(const FCommandList& InCopy) = delete;
		FCommandList(FCommandList&& InSteal) = delete;
		FCommandList& operator=(const FCommandList& InCopy) = delete;
		FCommandList& operator=(FCommandList&& InSteal) = delete;

		/**
		 * Enqueue functions for adding commands to be processed
		 */
		void Enqueue(IDispatcher::FGlobalCommand&& InCommand);
		void Enqueue(IDispatcher::FTaskCommand&& InCommand);
		void Enqueue(FPhysicsSolver* InSolver, IDispatcher::FSolverCommand&& InCommand);

		/**
		 * Submit this command list to the physics system. This will clear out the command queues in this
		 * object as they are moved out to the owner to handle the commands. A new empty list will be in
		 * place to accept further commands
		 *
		 * If there are no commands to be executed, no submission or allocation of a new list will be
		 * performed.
		 */
		void Flush();

	private:

		// Sets up the data ptr, used on init and after each flush
		void AllocData();
		
		// The actual commands, on flush this ptr is moved into the dispatcher for executioand a new
		// list data allocated to service future commands.
		TUniquePtr<FCommandListData> Data;
	};
}
