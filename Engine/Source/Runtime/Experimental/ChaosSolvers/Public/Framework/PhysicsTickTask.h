// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#include "Async/TaskGraphInterfaces.h"
#include "Dispatcher.h"
#include "Chaos/Declares.h"

class FChaosSolversModule;

/**
 * Task responsible for handling a full frame update for physics under the TaskGraph threading
 * mode.
 */
class CHAOSSOLVERS_API FPhysicsTickTask
{
public:

	FPhysicsTickTask(FGraphEventRef& InCompletionEvent, float InDt);

	TStatId GetStatId() const;
	static ENamedThreads::Type GetDesiredThread();
	static ESubsequentsMode::Type GetSubsequentsMode();
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

private:

	FGraphEventRef CompletionEvent;
	FChaosSolversModule* Module;
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

#endif
