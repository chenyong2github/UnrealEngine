// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterMessageInterceptor.h"

#include "Cluster/DisplayClusterClusterEvent.h"
#include "Misc/ScopeLock.h"
#include "IMessageContext.h"
#include "IMessageBus.h"
#include "Cluster/IDisplayClusterClusterManager.h"


DEFINE_LOG_CATEGORY_STATIC(LogDisplayClusterInterception, Log, All);

FDisplayClusterMessageInterceptor::FDisplayClusterMessageInterceptor()
	: bIsIntercepting(false)
	, InterceptorId(FGuid::NewGuid())
	, Address(FMessageAddress::NewAddress())
	, ClusterManager(nullptr)
{}

void FDisplayClusterMessageInterceptor::Setup(IDisplayClusterClusterManager* InClusterManager, TSharedPtr<IMessageBus, ESPMode::ThreadSafe> InBus)
{
	ClusterManager = InClusterManager;
	InterceptedBus = InBus;
}

void FDisplayClusterMessageInterceptor::Purge()
{
	TArray<TSharedPtr<IMessageContext, ESPMode::ThreadSafe>> ContextToForward;
	{
		FScopeLock Lock(&ContextQueueCS);
		for (const auto& ContextPair : ContextMap)
		{
			ContextToForward.Add(ContextPair.Value.ContextPtr);
		}
		ContextMap.Empty();
	}
	if (InterceptedBus)
	{
		for (const auto& Context : ContextToForward)
		{
			InterceptedBus->Forward(Context.ToSharedRef(), Context->GetRecipients(), FTimespan::Zero(), AsShared());
		}
	}
}

void FDisplayClusterMessageInterceptor::Start()
{
	const UDisplayClusterMessageInterceptionSettings* InterceptionSettings = GetDefault<UDisplayClusterMessageInterceptionSettings>();
	if (!bIsIntercepting && InterceptionSettings->bIsEnabled && InterceptedBus)
	{
		InterceptedAnnotation = InterceptionSettings->Annotation;
		FString DisplayString = InterceptedAnnotation.ToString();
		UE_LOG(LogDisplayClusterInterception, Display, TEXT("Starting interception of bus messages with annotation: %s"), *DisplayString);
		for (const FName& MessageType : InterceptionSettings->MessageTypes)
		{
			InterceptedBus->Intercept(AsShared(), MessageType);
			DisplayString = MessageType.ToString();
			UE_LOG(LogDisplayClusterInterception, Display, TEXT("Intercepted message type: %s"), *DisplayString);
		}
		bIsIntercepting = true;
	}
}

void FDisplayClusterMessageInterceptor::Stop()
{
	if (bIsIntercepting && InterceptedBus)
	{
		InterceptedBus->Unintercept(AsShared(), NAME_All);
		bIsIntercepting = false;
		Purge();
		UE_LOG(LogDisplayClusterInterception, Display, TEXT("Stopping interception of bus messages."));
	}
}

void FDisplayClusterMessageInterceptor::SyncMessages()
{
	if (ClusterManager)
	{
		TArray<FString> MessageIds;
		{
			FScopeLock Lock(&ContextQueueCS);
			ContextMap.GenerateKeyArray(MessageIds);
		}
		FDisplayClusterClusterEvent SyncMessagesEvent;
		SyncMessagesEvent.Category = TEXT("nDCI");				// message bus sync message
		SyncMessagesEvent.Name = ClusterManager->GetNodeId();	// which node got the message
		for (const FString& MessageId : MessageIds)
		{
			SyncMessagesEvent.Type = MessageId;					// the actually message id we received
			ClusterManager->EmitClusterEvent(SyncMessagesEvent, false);
			UE_LOG(LogDisplayClusterInterception, VeryVerbose, TEXT("Emitting cluster event for message %s on frame %d"), *MessageId, GFrameCounter);
		}
	}

	// remove out of date messages that are not marked reliable
	const FTimespan MessageTimeoutSpan = FTimespan(0, 0, 1);
	FDateTime UtcNow = FDateTime::UtcNow();

	TArray<TSharedPtr<IMessageContext, ESPMode::ThreadSafe>> ContextToForward;
	{
		FScopeLock Lock(&ContextQueueCS);
		for (auto It = ContextMap.CreateIterator(); It; ++It)
		{
			if (It.Value().ContextPtr->GetTimeSent() + MessageTimeoutSpan <= UtcNow)
			{
				if (!EnumHasAnyFlags(It.Value().ContextPtr->GetFlags(), EMessageFlags::Reliable))
				{
					UE_LOG(LogDisplayClusterInterception, VeryVerbose, TEXT("discarding unreliable message %s left intercepted for more than 1s"), *It.Key());
					It.RemoveCurrent();
				}
				else
				{
					UE_LOG(LogDisplayClusterInterception, Warning, TEXT("Forcing dispatching of reliable message %s left intercepted for more than 1s"), *It.Key());
					// Force treatment if not synced after a second and the message is reliable
					ContextToForward.Add(It.Value().ContextPtr);
				}
			}
		}
	}
	if (InterceptedBus)
	{
		for (const auto& Context : ContextToForward)
		{
			InterceptedBus->Forward(Context.ToSharedRef(), Context->GetRecipients(), FTimespan::Zero(), AsShared());
		}
	}
}

void FDisplayClusterMessageInterceptor::HandleClusterEvent(const FDisplayClusterClusterEvent& InEvent)
{
	TArray<TSharedPtr<IMessageContext, ESPMode::ThreadSafe>> ContextToForward;
	if (InEvent.Category == TEXT("nDCI") && ClusterManager)
	{
		FScopeLock Lock(&ContextQueueCS);
		if (FContextSync* ContextSync = ContextMap.Find(InEvent.Type))
		{
			ContextSync->NodesReceived.Add(InEvent.Name);
			if (ContextSync->NodesReceived.Num() >= (int32)ClusterManager->GetNodesAmount())
			{
				UE_LOG(LogDisplayClusterInterception, VeryVerbose, TEXT("Fowarding message for message id %s on frame %d"), *InEvent.Type, GFrameCounter);
				ContextToForward.Add(ContextSync->ContextPtr);
				ContextMap.Remove(InEvent.Type);
			}
		}
	}
	if (InterceptedBus)
	{
		for (const auto& Context : ContextToForward)
		{
			InterceptedBus->Forward(Context.ToSharedRef(), Context->GetRecipients(), FTimespan::Zero(), AsShared());
		}
	}
}

FName FDisplayClusterMessageInterceptor::GetDebugName() const
{
	return FName("DisplayClusterInterceptor");
}

const FGuid& FDisplayClusterMessageInterceptor::GetInterceptorId() const
{
	return InterceptorId;
}


bool FDisplayClusterMessageInterceptor::InterceptMessage(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	// we do not intercept forwarded message, they will be either coming off of the network or being forwarded by ourselves
	if (!bIsIntercepting || !Context->IsForwarded())
	{
		return false;
	}

	const FString* MessageId = Context->GetAnnotations().Find(InterceptedAnnotation);
	if (Context->GetForwarder() != Address && MessageId)
	{
		UE_LOG(LogDisplayClusterInterception, VeryVerbose, TEXT("Intercepting message %s"), **MessageId);

		FScopeLock Lock(&ContextQueueCS);
		ContextMap.Add(*MessageId, FContextSync(Context));
		return true;
	}
	return false;
}

FMessageAddress FDisplayClusterMessageInterceptor::GetSenderAddress()
{
	return Address;
}

void FDisplayClusterMessageInterceptor::NotifyMessageError(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context, const FString& Error)
{
	// deprecated
}
