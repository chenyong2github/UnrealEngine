// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMessageInterceptor.h"
#include "IMessageSender.h"
#include "DisplayClusterMessageInterceptionSettings.h"

class IMessageBus;
class IDisplayClusterClusterManager;
struct FDisplayClusterClusterEvent;

/**
 * Display Cluster Message Interceptor 
 * Intercept message bus messages to process them in sync across the display cluster
 */
class FDisplayClusterMessageInterceptor : public TSharedFromThis<FDisplayClusterMessageInterceptor, ESPMode::ThreadSafe>
	, public IMessageInterceptor
	, public IMessageSender
{
public:
	FDisplayClusterMessageInterceptor();

	void Setup(IDisplayClusterClusterManager* InClusterManager, TSharedRef<IMessageBus, ESPMode::ThreadSafe> InBus);
	
	void Start();
	void Stop();
	
	void SyncMessages();
	void HandleClusterEvent(const FDisplayClusterClusterEvent& InEvent);

	// IMessageInterceptor interface
	virtual FName GetDebugName() const override;
	virtual const FGuid& GetInterceptorId() const override;
	virtual bool InterceptMessage(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context) override;

	// IMessageSender interface
	virtual FMessageAddress GetSenderAddress() override;
	virtual void NotifyMessageError(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context, const FString& Error) override;
private:
	/** Purge the message map and force their forwarding. */
	void Purge();

	/** Currently intercepting message bus. */
	bool bIsIntercepting;

	/** InterceptorId needed by message bus. */
	FGuid InterceptorId;

	/** Sender address needed by message bus. */
	FMessageAddress Address;

	/** Message with this annotation will be intercepted. */
	FName InterceptedAnnotation;

	/** The bus this interceptor is intercepting messages on. */
	TSharedPtr<IMessageBus, ESPMode::ThreadSafe> InterceptedBus;

	/** The cluster manager to sync messages across. */
	IDisplayClusterClusterManager* ClusterManager;

	/** Critical section for the intercepted messages. */
	FCriticalSection ContextQueueCS;

	/** Holds intercepted messages and their current sync state across the cluster. */
	struct FContextSync
	{
		FContextSync(TSharedPtr<IMessageContext, ESPMode::ThreadSafe> InContext)
			: ContextPtr(InContext)
		{}

		TSet<FString> NodesReceived;
		TSharedPtr<IMessageContext, ESPMode::ThreadSafe> ContextPtr;
	};

	/** The intercepted message map. */
	TMap<FString, FContextSync> ContextMap;
};

