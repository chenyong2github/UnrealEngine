// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/DebugSubstep.h"

#if CHAOS_DEBUG_SUBSTEP

#include "ChaosLog.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"

namespace Chaos
{
	FDebugSubstep::FDebugSubstep()
		: bIsEnabled(false)
		, CommandQueue()
		, ProgressEvent(FPlatformProcess::GetSynchEventFromPool())
		, SubstepEvent(FPlatformProcess::GetSynchEventFromPool(true))  // SubstepEvent can be triggered without a matching wait, hence the manual setting
		, bWaitForStep(false)
		, ThreadId(0)
	{}

	FDebugSubstep::~FDebugSubstep()
	{
		FPlatformProcess::ReturnSynchEventToPool(ProgressEvent);
		FPlatformProcess::ReturnSynchEventToPool(SubstepEvent);
	}

	void FDebugSubstep::Enable(bool bEnable)
	{
		CommandQueue.Enqueue(bEnable ? ECommand::Enable: ECommand::Disable);
		UE_LOG(LogChaosThread, Verbose, TEXT("[Game Thread] Enable=%s"), bEnable ? TEXT("True"): TEXT("False"));
	}

	void FDebugSubstep::ProgressToSubstep()
	{
		CommandQueue.Enqueue(ECommand::ProgressToSubstep);
		UE_LOG(LogChaosThread, Verbose, TEXT("[Game Thread] Progress"));
	}

	void FDebugSubstep::ProgressToStep()
	{
		CommandQueue.Enqueue(ECommand::ProgressToStep);
		UE_LOG(LogChaosThread, Verbose, TEXT("[Game Thread] Progress"));
	}

	void FDebugSubstep::Shutdown()
	{
		if (bIsEnabled)
		{
			bIsEnabled = false;
			UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] bIsEnabled changed (true->false)"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));

			// Trigger progress, with bIsEnabled being false it should go straight to the end of the step
			SubstepEvent->Reset();     // No race condition with the Add() between these two instructions
			ProgressEvent->Trigger();  // since this code path is only to be entered while in ProgressEvent->Wait(); state.

			// Wait for the final step event
			UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] Waiting for last step event"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));
			SubstepEvent->Wait();  // Last wait event will be triggered on debug thread exit
			UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] Substep event received, wait ended"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));
			UE_LOG(LogChaosThread, Log, TEXT("Chaos' debug substep mode is now disengaged. Resuming solver thread at next step."));
		}
		// Empty queue
		CommandQueue.Empty();
	}

	bool FDebugSubstep::SyncAdvance()
	{
		ECommand Command;
		while (CommandQueue.Dequeue(Command))
		{
			switch (Command)
			{
			case ECommand::Enable:
				if (!bIsEnabled)
				{
					bIsEnabled = true;
					UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] bIsEnabled changed (false->true)"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));
					UE_LOG(LogChaosThread, Log, TEXT("Chaos' debug substep mode is now engaged. Pausing solver thread at next step."));
				}
				return true;  // Wait until thread has started before dequeuing more commands

			case ECommand::Disable:
				if (bIsEnabled)
				{
					bIsEnabled = false;
					UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] bIsEnabled changed (true->false)"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));

					// Trigger progress, with bIsEnabled being false it should go straight to the end of the step
					SubstepEvent->Reset();     // No race condition with the Add() between these two instructions
					ProgressEvent->Trigger();  // since this code path is only to be entered while in ProgressEvent->Wait(); state.

					// Wait for the final step event
					UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] Waiting for last step event"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));
					SubstepEvent->Wait();  // Last wait event will be triggered on debug thread exit
					UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] Substep event received, wait ended"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));
					UE_LOG(LogChaosThread, Log, TEXT("Chaos' debug substep mode is now disengaged. Resuming solver thread at next step."));
				}
				return false;  // Wait until thread has ended before dequeuing more commands

			case ECommand::ProgressToStep:  // Intentional fallthrough
			case ECommand::ProgressToSubstep:
				if (bIsEnabled)
				{
					UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] Triggering progress event"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));
					bWaitForStep = (Command == ECommand::ProgressToStep);
					do
					{
						// Trigger progress
						SubstepEvent->Reset();     // No race condition with the Add() between these two instructions
						ProgressEvent->Trigger();  // since this code path is only to be entered while in ProgressEvent->Wait(); state.

						// Wait for next step/substep event
						UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] Waiting for substep event"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));
						SubstepEvent->Wait();
						UE_LOG(LogChaosThread, Verbose, TEXT("[%s Thread] Substep event received, wait ended"), IsInGameThread() ? TEXT("Game"): TEXT("Physics"));
					}
					while (bWaitForStep);
				}
				break;

			default: 
				UE_LOG(LogChaosThread, Fatal, TEXT("Unknown debug step command."));
				break;
			}
		}
		return bIsEnabled;
	}

	void FDebugSubstep::AssumeThisThread()
	{
		ThreadId = FPlatformTLS::GetCurrentThreadId();
	}

	void FDebugSubstep::Add(bool bInStep, const TCHAR* Label) const
	{
		if (bIsEnabled)
		{
			if (bInStep) { bWaitForStep = false; }
			checkf(ThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("Cannot add a substep outside of the solver thread (eg inside a parallel for)."));
			UE_LOG(LogChaosThread, Log, TEXT("Reached %s '%s'"), bInStep ? TEXT("step"): TEXT("substep"), Label ? Label: TEXT(""));
			UE_LOG(LogChaosThread, Verbose, TEXT("[Debug Thread] Triggering substep event"));
			SubstepEvent->Trigger();
			UE_LOG(LogChaosThread, Verbose, TEXT("[Debug Thread] Waiting for progress event"));
			ProgressEvent->Wait();
			UE_LOG(LogChaosThread, Verbose, TEXT("[Debug Thread] Progress event received, wait ended"));
		}
		else if (bInStep)  // Trigger one last event at the step boundary when disabled
		{
			UE_LOG(LogChaosThread, Log, TEXT("Reached step '%s'"),  Label ? Label: TEXT(""));
			UE_LOG(LogChaosThread, Verbose, TEXT("[Debug Thread] Triggering substep event"));
			SubstepEvent->Trigger();
		}
	}
}

#endif  // #if CHAOS_DEBUG_SUBSTEP
