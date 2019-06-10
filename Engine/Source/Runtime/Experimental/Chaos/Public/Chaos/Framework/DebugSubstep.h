// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Build.h"

#ifndef CHAOS_DEBUG_SUBSTEP
#define CHAOS_DEBUG_SUBSTEP INCLUDE_CHAOS && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

#if CHAOS_DEBUG_SUBSTEP

#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"

class FEvent;

namespace Chaos
{
	/** Debug only class used to control the pausing/stepping/substepping of a debug solver thread. */
	class CHAOS_API FDebugSubstep final
	{
		friend class FDebugSolverTask;
		friend class FDebugSolverTasks;

	public:
		FDebugSubstep();
		~FDebugSubstep();

		/** Return whether debugging mode/pausing to substeps is enabled. */
		bool IsEnabled() const { return bIsEnabled; }

		/**
		 * Add a new potential pause point where the debug solver thread can wait until the next step/substep command.
		 * @param Label The reference (if any) that will be used in verbose logs when this point is reached, or nullptr otherwise.
		 * Only call from the solver thread.
		 * It will fail if called from inside a parallel for loop, or any other thread.
		 */
		FORCEINLINE void Add(const TCHAR* Label = nullptr) const { Add(false, Label); }

		/** Enable/disable substep pause points. */
		void Enable(bool bEnable);

		/** Allow progress to the next substep (works only after this object is enabled). */
		void ProgressToSubstep();

		/** Allow progress to the next step (works only after this object is enabled). */
		void ProgressToStep();

		/*
		 * Disable and wait for the completion of the debug thread.
		 * Not thread safe. Must be either called from within the physics thread 
		 * or within the game thread with the physics thread locked.
		 */
		void Shutdown();

	private:
		FDebugSubstep(const FDebugSubstep&) = delete;
		FDebugSubstep& operator=(const FDebugSubstep&) = delete;

		/**
		 * Control substepping progress.
		 * Start substepping, wait until the next substep is reached, or return straightaway if debugging is disabled.
		 * @return Whether the debug thread needs running.
		 */
		bool SyncAdvance();

		/** Set the id of the thread the debug substepping will be running in. */
		void AssumeThisThread();

		/**
		 * Add a new step or substep.
		 * @param bInStep Add a step instead of a substep when this is true (for internal use only in the solver debug thread loop).
		 * @param Label The reference (if any) that will be used in verbose logs when this point is reached, or nullptr otherwise.
		 */
		void Add(bool bInStep, const TCHAR* Label) const;

	private:
		enum class ECommand { Enable, Disable, ProgressToSubstep, ProgressToStep };

		FThreadSafeBool bIsEnabled;    // Status of the debugging thread. Can find itself in a race condition while the Add() method is ran outside of the debug thread, hence the FThreadSafeBool.
		TQueue<ECommand, EQueueMode::Mpsc> CommandQueue;  // Command queue, thread safe, Multiple-producers single-consumer (MPSC) model.
		FEvent* ProgressEvent;         // Progress synchronization event.
		FEvent* SubstepEvent;          // Substep synchronization event.
		mutable bool bWaitForStep;     // Boolean used to flag the completion of a step. Set within the a const function, hence the mutable.
		uint32 ThreadId;               // Thread id used to check that the debug substep code is still running within the debug thread.
	};
}

#else  // #if CHAOS_DEBUG_SUBSTEP

#include "HAL/Platform.h"

namespace Chaos
{
	/**
	 * Debug substep stub for non debug builds.
	 */
	class FDebugSubstep final
	{
	public:
		FDebugSubstep() {}
		~FDebugSubstep() {}

		bool IsEnabled() const { return false; }

		void Add(const TCHAR* /*Label*/ = nullptr) {}

		void Enable(bool /*bEnable*/) {}
		void Progress() {}

	private:
		FDebugSubstep(const FDebugSubstep&) = delete;
		FDebugSubstep& operator=(const FDebugSubstep&) = delete;
	};
}

#endif  // #if CHAOS_DEBUG_SUBSTEP #else
