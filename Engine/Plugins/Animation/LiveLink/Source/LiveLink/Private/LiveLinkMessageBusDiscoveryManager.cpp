// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkMessageBusDiscoveryManager.h"

#include "Async/Async.h"
#include "ILiveLinkClient.h"
#include "LiveLinkMessageBusSource.h"
#include "LiveLinkSettings.h"
#include "HAL/PlatformTime.h"
#include "MessageEndpointBuilder.h"
#include "Misc/ScopeLock.h"


#define LL_HEARTBEAT_SLEEP_TIME 1.0f

FLiveLinkMessageBusDiscoveryManager* FLiveLinkMessageBusDiscoveryManager::Instance = nullptr;

FLiveLinkMessageBusDiscoveryManager::FLiveLinkMessageBusDiscoveryManager()
	: bRunning(true)
{
	PingRequestCounter = 0;
	PingRequestFrequency = GetDefault<ULiveLinkSettings>()->GetMessageBusPingRequestFrequency();

	MessageEndpoint = FMessageEndpoint::Builder(TEXT("LiveLinkMessageHeartbeatManager"))
		.Handling<FLiveLinkPongMessage>(this, &FLiveLinkMessageBusDiscoveryManager::HandlePongMessage);

	Thread = FRunnableThread::Create(this, TEXT("MessageBusHeartbeatManager"));
}

FLiveLinkMessageBusDiscoveryManager::~FLiveLinkMessageBusDiscoveryManager()
{
	{
		FScopeLock Lock(&SourcesCriticalSection);
		
		// Disable the Endpoint message handling since the message could keep it alive a bit.
		MessageEndpoint->Disable();
		MessageEndpoint.Reset();
	}

	Stop();
	if (Thread)
	{
		Thread->Kill(true);
		Thread = nullptr;
	}
};

FLiveLinkMessageBusDiscoveryManager* FLiveLinkMessageBusDiscoveryManager::Get()
{
	if (Instance == nullptr)
	{
		Instance = new FLiveLinkMessageBusDiscoveryManager();
	}
	return Instance;
}

uint32 FLiveLinkMessageBusDiscoveryManager::Run()
{
	while (bRunning)
	{
		{
			FScopeLock Lock(&SourcesCriticalSection);

			if (PingRequestCounter > 0)
			{
				LastProviderPoolResults.Reset();
				LastPingRequest = FGuid::NewGuid();
				MessageEndpoint->Publish(new FLiveLinkPingMessage(LastPingRequest, ILiveLinkClient::LIVELINK_VERSION));
			}
		}

		FPlatformProcess::Sleep(PingRequestFrequency);
	}
	return 0;
};

void FLiveLinkMessageBusDiscoveryManager::Stop()
{
	bRunning = false;
}

void FLiveLinkMessageBusDiscoveryManager::AddDiscoveryMessageRequest()
{
	FScopeLock Lock(&SourcesCriticalSection);
	if (++PingRequestCounter == 1)
	{
		LastProviderPoolResults.Reset();
	}
}

void FLiveLinkMessageBusDiscoveryManager::RemoveDiscoveryMessageRequest()
{
	--PingRequestCounter;
}

TArray<FProviderPollResultPtr> FLiveLinkMessageBusDiscoveryManager::GetDiscoveryResults() const
{
	FScopeLock Lock(&SourcesCriticalSection);
	return LastProviderPoolResults;
}

bool FLiveLinkMessageBusDiscoveryManager::IsRunning() const
{
	return bRunning;
}

void FLiveLinkMessageBusDiscoveryManager::HandlePongMessage(const FLiveLinkPongMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	FScopeLock Lock(&SourcesCriticalSection);

	if (Message.PollRequest == LastPingRequest)
	{
		LastProviderPoolResults.Emplace(MakeShared<FProviderPollResult, ESPMode::ThreadSafe>(Context->GetSender(), Message.ProviderName, Message.MachineName));
	}
}