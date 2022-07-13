// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/RenderPageQueue.h"


void UE::RenderPages::Private::FRenderPageQueue::Tick(float DeltaTime)
{
	if (DelayRemainingFrames > 0)
	{
		DelayRemainingFrames--;
	}
	if (DelayRemainingSeconds > 0)
	{
		DelayRemainingSeconds -= DeltaTime;
	}

	if (DelayRemainingFuture.IsValid())
	{
		if (!DelayRemainingFuture.IsReady())
		{
			return;
		}
		DelayRemainingFuture = TFuture<void>();
	}
	if (DelayRemainingFutureReturningDelay.IsValid())
	{
		if (!DelayRemainingFutureReturningDelay.IsReady())
		{
			return;
		}
		QueueDelay(DelayRemainingFutureReturningDelay.Get());
		DelayRemainingFutureReturningDelay = TFuture<FRenderPageQueueDelay>();
	}

	if (!bExecuting || ((DelayRemainingFrames <= 0) && (DelayRemainingSeconds <= 0)))
	{
		ExecuteNext();
	}
}

void UE::RenderPages::Private::FRenderPageQueue::Start()
{
	bStarted = true;
}

void UE::RenderPages::Private::FRenderPageQueue::Stop()
{
	bStarted = false;
}

void UE::RenderPages::Private::FRenderPageQueue::ExecuteNext()
{
	bExecuting = true;
	while (true)
	{
		if (DelayRemainingFuture.IsValid() || DelayRemainingFutureReturningDelay.IsValid())
		{
			// continue in Tick event
			return;
		}
		if (ExecuteNextDelay())
		{
			// continue in Tick event
			return;
		}
		if (bStarted && ExecuteNextEntry())
		{
			// execution completed, restart this loop  (to check for new delays, and to execute the next entry)
			continue;
		}
		break;
	}
	// if it gets to this point, it means that there are no delays or executions anymore  (since it would have returned out of this function otherwise)
	bExecuting = false;
}

bool UE::RenderPages::Private::FRenderPageQueue::ExecuteNextDelay()
{
	while (true)
	{
		if (TDoubleLinkedList<FRenderPageQueueDelay>::TDoubleLinkedListNode* DelayNode = QueuedDelays.GetHead())
		{
			const FRenderPageQueueDelay& Delay = DelayNode->GetValue();
			if (Delay.MinimumFrames > 0)
			{
				DelayRemainingFrames = Delay.MinimumFrames;
			}
			if (Delay.MinimumSeconds > 0)
			{
				DelayRemainingSeconds = Delay.MinimumSeconds;
			}
			QueuedDelays.RemoveNode(DelayNode);
			if ((DelayRemainingFrames <= 0) && (DelayRemainingSeconds <= 0))
			{
				continue;
			}
			return true;
		}
		return false;
	}
}

bool UE::RenderPages::Private::FRenderPageQueue::ExecuteNextEntry()
{
	if (TDoubleLinkedList<FRenderPageQueueEntry>::TDoubleLinkedListNode* EntryNode = QueuedEntries.GetHead())
	{
		const FRenderPageQueueEntry& Entry = EntryNode->GetValue();

		Entry.ActionRegular.ExecuteIfBound();

		if (Entry.ActionReturningDelay.IsBound())
		{
			QueueDelay(Entry.ActionReturningDelay.Execute());
		}

		if (Entry.ActionReturningDelayFuture.IsBound())
		{
			DelayRemainingFuture = Entry.ActionReturningDelayFuture.Execute();
		}

		if (Entry.ActionReturningDelayFutureReturningDelay.IsBound())
		{
			DelayRemainingFutureReturningDelay = Entry.ActionReturningDelayFutureReturningDelay.Execute();
		}

		QueuedEntries.RemoveNode(EntryNode);
		return true;
	}
	return false;
}
