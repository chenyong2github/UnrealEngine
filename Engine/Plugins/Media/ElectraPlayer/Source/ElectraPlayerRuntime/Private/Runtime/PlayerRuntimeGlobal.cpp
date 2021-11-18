// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "PlayerRuntimeGlobal.h"
#include "Misc/CoreDelegates.h"

namespace Electra
{
	namespace Global
	{
		TMap<FString, bool>								EnabledAnalyticsEvents;
		FDelegateHandle									ApplicationSuspendedDelegate;
		FDelegateHandle									ApplicationResumeDelegate;
		FCriticalSection								ApplicationBGFGLock;
		TArray<TWeakPtrTS<FFGBGNotificationHandlers>>	ApplicationBGFGHandlers;
		volatile bool									bIsInBackground = false;
	}
	using namespace Global;


	static void HandleApplicationWillEnterBackground()
	{
		ApplicationBGFGLock.Lock();
		TArray<TWeakPtrTS<FFGBGNotificationHandlers>>	CurrentHandlers(ApplicationBGFGHandlers);
		bIsInBackground = true;
		ApplicationBGFGLock.Unlock();
		for(auto &Handler : CurrentHandlers)
		{
			TSharedPtrTS<FFGBGNotificationHandlers> Hdlr = Handler.Pin();
			if (Hdlr.IsValid())
			{
				Hdlr->WillEnterBackground();
			}
		}
	}

	static void HandleApplicationHasEnteredForeground()
	{
		ApplicationBGFGLock.Lock();
		TArray<TWeakPtrTS<FFGBGNotificationHandlers>>	CurrentHandlers(ApplicationBGFGHandlers);
		bIsInBackground = false;
		ApplicationBGFGLock.Unlock();
		for(auto &Handler : CurrentHandlers)
		{
			TSharedPtrTS<FFGBGNotificationHandlers> Hdlr = Handler.Pin();
			if (Hdlr.IsValid())
			{
				Hdlr->HasEnteredForeground();
			}
		}
	}


	//-----------------------------------------------------------------------------
	/**
	 * Initializes core service functionality. Memory hooks must have been registered before calling this function.
	 *
	 * @param configuration
	 *
	 * @return
	 */
	bool Startup(const Configuration& InConfiguration)
	{
		if (!ApplicationSuspendedDelegate.IsValid())
		{
			ApplicationSuspendedDelegate = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddStatic(&HandleApplicationWillEnterBackground);
		}
		if (!ApplicationResumeDelegate.IsValid())
		{
			ApplicationResumeDelegate = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddStatic(&HandleApplicationHasEnteredForeground);
		}

		EnabledAnalyticsEvents = InConfiguration.EnabledAnalyticsEvents;
		return(true);
	}


	//-----------------------------------------------------------------------------
	/**
	 * Shuts down core services.
	 */
	void Shutdown(void)
	{
		if (ApplicationSuspendedDelegate.IsValid())
		{
			FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(ApplicationSuspendedDelegate);
			ApplicationSuspendedDelegate.Reset();
		}
		if (ApplicationResumeDelegate.IsValid())
		{
			FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ApplicationResumeDelegate);
			ApplicationResumeDelegate.Reset();
		}
	}

	bool AddBGFGNotificationHandler(TSharedPtrTS<FFGBGNotificationHandlers> InHandlers)
	{
		FScopeLock lock(&ApplicationBGFGLock);
		ApplicationBGFGHandlers.Add(InHandlers);
		return bIsInBackground;
	}

	void RemoveBGFGNotificationHandler(TSharedPtrTS<FFGBGNotificationHandlers> InHandlers)
	{
		FScopeLock lock(&ApplicationBGFGLock);
		ApplicationBGFGHandlers.Remove(InHandlers);
	}


	/**
	 * Check if the provided analytics event is enabled
	 *
	 * @param AnalyticsEventName of event to check
	 * @return true if event is found and is set to true
	 */
	bool IsAnalyticsEventEnabled(const FString& AnalyticsEventName)
	{
		const bool* bEventEnabled = EnabledAnalyticsEvents.Find(AnalyticsEventName);
		return bEventEnabled && *bEventEnabled;
	}
	
	class PendingTaskCounter
	{
	public:
		PendingTaskCounter() : AllDoneSignal(nullptr), NumPendingTasks(0)
		{
			// Note: we only initialize the done signal on first adding a task etc.
			// to avoid a signal to be used during the global constructor phase
			// (too early for UE)
		}

		~PendingTaskCounter()
		{
		}

		//! Adds a pending task.
		void AddTask()
		{
			Init();
			if (FMediaInterlockedIncrement(NumPendingTasks) == 0)
			{
				AllDoneSignal->Reset();
			}
		}

		//! Removes a pending task when it's done. Returns true when this was the last task, false otherwise.
		bool RemoveTask()
		{
			Init();
			if (FMediaInterlockedDecrement(NumPendingTasks) == 1)
			{
				AllDoneSignal->Signal();
				return true;
			}
			else
			{
				return false;
			}
		}

		//! Waits for all pending tasks to have finished. Once all are done new tasks cannot be added.
		void WaitAllFinished()
		{
			Init();
			AllDoneSignal->Wait();
		}

		void Reset()
		{
			delete AllDoneSignal;
			AllDoneSignal = nullptr;
		}

	private:
		void Init()
		{
			// Initialize our signal event if we don't have it already...
			if (!AllDoneSignal)
			{
				FMediaEvent* NewSignal = new FMediaEvent();
				if (TMediaInterlockedExchangePointer(AllDoneSignal, NewSignal) != nullptr)
				{
					delete NewSignal;
				}
				// The new signal must be set initially to allow for WaitAllFinished() to leave
				// without any task having been added. It gets cleared on the first AddTask().
				AllDoneSignal->Signal();
			}
		}

		FMediaEvent* AllDoneSignal;
		int32		NumPendingTasks;
	};




	static PendingTaskCounter NumActivePlayers;


	void WaitForAllPlayersToHaveTerminated()
	{
		NumActivePlayers.WaitAllFinished();
		// Explicitly shutdown anything in the counter class that may use the engine (as it might shutdown after this)
		NumActivePlayers.Reset();
	}

	void AddActivePlayerInstance()
	{
		NumActivePlayers.AddTask();
	}

	void RemoveActivePlayerInstance()
	{
		NumActivePlayers.RemoveTask();
	}


};


