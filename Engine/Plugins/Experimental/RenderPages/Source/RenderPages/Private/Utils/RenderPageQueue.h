// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Containers/List.h"
#include "Tickable.h"


namespace UE::RenderPages::Private
{
	/**
	 * Struct containing the delay data (such as the frames or the seconds of delay this delay requires before it can move on to the next step).
	 */
	struct RENDERPAGES_API FRenderPageQueueDelay
	{
	public:
		static FRenderPageQueueDelay Frames(const int64 Frames) { return FRenderPageQueueDelay(Frames); }
		static FRenderPageQueueDelay Seconds(const double Seconds) { return FRenderPageQueueDelay(Seconds); }
		static FRenderPageQueueDelay FramesOrSeconds(const int64 Frames, const double Seconds) { return FRenderPageQueueDelay(Frames, Seconds); }

	public:
		FRenderPageQueueDelay(TYPE_OF_NULLPTR)
		{}

	private:
		FRenderPageQueueDelay()
		{}

		explicit FRenderPageQueueDelay(const int64 Frames)
			: MinimumFrames(Frames)
		{}

		explicit FRenderPageQueueDelay(const double Seconds)
			: MinimumSeconds(Seconds)
		{}

		explicit FRenderPageQueueDelay(const int64 Frames, const double Seconds)
			: MinimumFrames(Frames)
			, MinimumSeconds(Seconds)
		{}

	public:
		const int64 MinimumFrames = 0;
		const double MinimumSeconds = 0.0f;
	};


	/** A delegate for a queued action. */
	DECLARE_DELEGATE(FRenderPageQueueAction);

	/** A delegate for a queued action, that optionally requires a delay after its execution. */
	DECLARE_DELEGATE_RetVal(FRenderPageQueueDelay, FRenderPageQueueActionReturningDelay);

	/** A delegate for a queued action, that will delay execution until the returned TFuture finishes. */
	DECLARE_DELEGATE_RetVal(TSharedFuture<void>, FRenderPageQueueActionReturningDelayFuture);

	/** A delegate for a queued action, that will delay execution until the returned TFuture finishes, which can optionally return yet another delay if required. */
	DECLARE_DELEGATE_RetVal(TSharedFuture<FRenderPageQueueDelay>, FRenderPageQueueActionReturningDelayFutureReturningDelay);

	/**
	 * Struct containing the data of a queued action.
	 */
	struct RENDERPAGES_API FRenderPageQueueEntry
	{
	public:
		FRenderPageQueueEntry(const FRenderPageQueueAction& Action)
			: ActionRegular(Action)
		{}

		FRenderPageQueueEntry(const FRenderPageQueueActionReturningDelay& Action)
			: ActionReturningDelay(Action)
		{}

		FRenderPageQueueEntry(const FRenderPageQueueActionReturningDelayFuture& Action)
			: ActionReturningDelayFuture(Action)
		{}

		FRenderPageQueueEntry(const FRenderPageQueueActionReturningDelayFutureReturningDelay& Action)
			: ActionReturningDelayFutureReturningDelay(Action)
		{}

	public:
		const FRenderPageQueueAction ActionRegular;
		const FRenderPageQueueActionReturningDelay ActionReturningDelay;
		const FRenderPageQueueActionReturningDelayFuture ActionReturningDelayFuture;
		const FRenderPageQueueActionReturningDelayFutureReturningDelay ActionReturningDelayFutureReturningDelay;
	};


	/**
	 * This class provides generic queue support, with built-in support for delays between actions.
	 */
	class RENDERPAGES_API FRenderPageQueue : public TSharedFromThis<FRenderPageQueue>, public FTickableGameObject
	{
	public:
		//~ Begin FTickableGameObject interface
		virtual void Tick(float DeltaTime) override;
		virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
		virtual bool IsTickableWhenPaused() const override { return true; }
		virtual bool IsTickableInEditor() const override { return true; }
		virtual bool IsTickable() const override { return true; }
		virtual bool IsAllowedToTick() const override { return true; }
		virtual TStatId GetStatId() const override
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FRenderPageQueue, STATGROUP_Tickables);
		}
		//~ End FTickableGameObject interface

	public:
		/** Queues the given action. */
		void Add(const FRenderPageQueueEntry& Entry) { QueuedEntries.AddTail(Entry); }

		/** Queues the given delay. */
		void Delay(const FRenderPageQueueDelay& Delay) { Add(FRenderPageQueueActionReturningDelay::CreateLambda([Delay]() -> FRenderPageQueueDelay { return Delay; })); }

		/** Queues the given delay, which will wait for the given number of frames. */
		void DelayFrames(const int64 Frames) { Delay(FRenderPageQueueDelay::Frames(Frames)); }

		/** Queues the given delay, which will wait for the given number of seconds. */
		void DelaySeconds(const double Seconds) { Delay(FRenderPageQueueDelay::Seconds(Seconds)); }

		/** Queues the given delay, which will wait for the given number of frames or seconds, whatever takes the longest. */
		void DelayFramesOrSeconds(const int64 Frames, const double Seconds) { Delay(FRenderPageQueueDelay::FramesOrSeconds(Frames, Seconds)); }

		/** Starts the execution of this queue. */
		void Start();

		/** Stops (pauses) the execution of this queue, this can be resumed by calling the Start function again. Currently queued up delays will continue to run/expire. */
		void Stop();

		/** Returns true if Start() has been called and Stop() hasn't been called yet. */
		bool IsRunning() { return bStarted; }

	protected:
		/** Executes the next delay (if there are any), otherwise it executes the next entry (action). */
		void ExecuteNext();

		/** Executes the next delay, returns true if it found any, returns false if there were no queued up delays. */
		bool ExecuteNextDelay();

		/** Executes the next entry (action), returns true if it found and executed an entry, returns false if there were no queued up entries. */
		bool ExecuteNextEntry();

		/** Adds the delay to the queued delays. */
		void QueueDelay(const FRenderPageQueueDelay& Delay) { QueuedDelays.AddTail(Delay); }

	protected:
		/** The queued up entries (actions). */
		TDoubleLinkedList<FRenderPageQueueEntry> QueuedEntries;

		/** The queued up delays. */
		TDoubleLinkedList<FRenderPageQueueDelay> QueuedDelays;

	protected:
		/** Whether it has started (and hasn't been stopped/paused yet). This means that if this is true, Start() has been called, and Stop() hasn't been called yet since then. */
		bool bStarted = false;

		/** Whether it's currently executing a delay or an entry (action). False means there were no delays and entries queued up anymore. */
		bool bExecuting = false;

		/** The number of frames the current delay has to wait for. */
		int64 DelayRemainingFrames = 0;

		/** The number of seconds the current delay has to wait for. */
		double DelayRemainingSeconds = 0.0f;

		/** The TFuture it's waiting for (if any). */
		TSharedFuture<void> DelayRemainingFuture;

		/** The TFuture it's waiting for (if any), that can return a delay. */
		TSharedFuture<FRenderPageQueueDelay> DelayRemainingFutureReturningDelay;
	};
}
