// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Async/TaskGraphInterfaces.h"
#include "Chaos/Declares.h"

class FChaosSolversModule;

namespace Chaos
{
	class IDispatcher;
}

/**
 * Task responsible for handling a full frame update for physics under the TaskGraph threading
 * mode. Multiple ways to construct this for various situations depending on the subset
 * of solvers the caller wishes to update.
 */
class CHAOSSOLVERS_API FPhysicsTickTask
{
public:

	/** Construct a task that will tick all solvers in the solver module */
	FPhysicsTickTask(FGraphEventRef& InCompletionEvent, float InDt);
	/** Construct a task that will tick the provided solver (or all solvers in the module if nullptr passed) */
	FPhysicsTickTask(FGraphEventRef& InCompletionEvent, Chaos::FPhysicsSolver* InPhysicsSolver, float InDt);
	/** Construct a task to tick the provided list of solvers */
	FPhysicsTickTask(FGraphEventRef& InCompletionEvent, const TArray<Chaos::FPhysicsSolver*>& InSolverList, float InDt);

	/** Task API */
	TStatId GetStatId() const;
	static ENamedThreads::Type GetDesiredThread();
	static ESubsequentsMode::Type GetSubsequentsMode();
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);
	/** End Task API */

private:

	/** Any prerequisites when this task was constructed for the solver ticks to obey */
	FGraphEventArray SolverTaskPrerequisites;

	/** An event to dispatch once completed to signal the calling thread */
	FGraphEventRef CompletionEvent;

	/** Solver module containing master solver lists */
	FChaosSolversModule* Module;

	/** The solvers this task will tick */
	TArray<Chaos::FPhysicsSolver*> SolverList;

	/** Delta time for the solver tick */
	float Dt;
};

/**
 * Task responsible for running the two global command queues prior to distributing the solver tasks.
 * The base tick task will dispatch this task then begin dispatching solvers while this is ongoing.
 */
class CHAOSSOLVERS_API FPhysicsCommandsTask
{
public:

	FPhysicsCommandsTask();

	TStatId GetStatId() const;
	static ENamedThreads::Type GetDesiredThread();
	static ESubsequentsMode::Type GetSubsequentsMode();
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

private:

	FChaosSolversModule* Module;
	Chaos::IDispatcher* Dispatcher;
};

/**
 * Task responsible for processing the command buffer of a single solver and advancing it by
 * a specified delta before completing.
 */
class CHAOSSOLVERS_API FPhysicsSolverAdvanceTask
{
public:

	FPhysicsSolverAdvanceTask(Chaos::FPhysicsSolver* InSolver, float InDt);

	TStatId GetStatId() const;
	static ENamedThreads::Type GetDesiredThread();
	static ESubsequentsMode::Type GetSubsequentsMode();
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

private:

	void StepSolver(Chaos::FPhysicsSolver* InSolver, float InDt);

	Chaos::FPhysicsSolver* Solver;
	float Dt;
};

/**
 * Task responsible for processing the command buffer of a single solver and advancing it
 * the specified number of times, by the specified delta time.
 */
class CHAOSSOLVERS_API FPhysicsSolverAdvanceSubsteppingTask
{
public:

	FPhysicsSolverAdvanceSubsteppingTask(Chaos::FPhysicsSolver* InSolver, int32 NumIterations, float InDtPerIteration);

	TStatId GetStatId() const;
	static ENamedThreads::Type GetDesiredThread();
	static ESubsequentsMode::Type GetSubsequentsMode();
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

private:

	void StepSolver(Chaos::FPhysicsSolver* InSolver, float Dt);

	FGraphEventRef CompletionEvent;
	FChaosSolversModule* Module;
	float Dt;
};

/**
 * Final threaded task to run, waits on all the solver ticks and triggers the final completion event
 */
class CHAOSSOLVERS_API FPhysicsTickCompleteTask
{
public:

	FPhysicsTickCompleteTask(FGraphEventRef& InCompletionEvent);

	TStatId GetStatId() const;
	static ENamedThreads::Type GetDesiredThread();
	static ESubsequentsMode::Type GetSubsequentsMode();
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

private:

	FGraphEventRef CompletionEvent;

};
