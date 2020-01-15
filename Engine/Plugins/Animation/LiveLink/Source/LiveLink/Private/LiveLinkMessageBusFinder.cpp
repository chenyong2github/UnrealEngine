// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkMessageBusFinder.h"

#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "LiveLinkClient.h"
#include "LiveLinkMessages.h"
#include "LiveLinkMessageBusSource.h"
#include "LiveLinkMessageBusSourceFactory.h"
#include "MessageEndpointBuilder.h"


namespace LiveLinkMessageBusHelper
{
	double CalculateProviderMachineOffset(double SourceMachinePlatformSeconds, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
	{
		const FDateTime Now = FDateTime::UtcNow();
		const double MyMachineSeconds = FPlatformTime::Seconds();
		const FTimespan Latency = Now - Context->GetTimeSent();
		double MachineTimeOffset = 0.0f;
		if (SourceMachinePlatformSeconds >= 0.0)
		{
			MachineTimeOffset = MyMachineSeconds - SourceMachinePlatformSeconds - Latency.GetTotalSeconds();
		}

		return MachineTimeOffset;
	}
}


ULiveLinkMessageBusFinder::ULiveLinkMessageBusFinder()
	: MessageEndpoint(nullptr)
{

};

void ULiveLinkMessageBusFinder::GetAvailableProviders(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, float Duration, TArray<FProviderPollResult>& AvailableProviders)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		FLatentActionManager& LatentManager = World->GetLatentActionManager();
		if (LatentManager.FindExistingAction<FLiveLinkMessageBusFinderAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			PollNetwork();

			FLiveLinkMessageBusFinderAction* NewAction = new FLiveLinkMessageBusFinderAction(LatentInfo, this, Duration, AvailableProviders);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GetAvailableProviders not executed. The previous action hasn't finished yet."));
		}
	}
};

void ULiveLinkMessageBusFinder::GetPollResults(TArray<FProviderPollResult>& AvailableProviders)
{
	FScopeLock ScopedLock(&PollDataCriticalSection);
	AvailableProviders = PollData;
};

void ULiveLinkMessageBusFinder::PollNetwork()
{
	if (!MessageEndpoint.IsValid())
	{
		MessageEndpoint = FMessageEndpoint::Builder(TEXT("LiveLinkMessageBusSource"))
			.Handling<FLiveLinkPongMessage>(this, &ULiveLinkMessageBusFinder::HandlePongMessage);
	}

	PollData.Reset();
	CurrentPollRequest = FGuid::NewGuid();
	MessageEndpoint->Publish(new FLiveLinkPingMessage(CurrentPollRequest, ILiveLinkClient::LIVELINK_VERSION));
};

void ULiveLinkMessageBusFinder::HandlePongMessage(const FLiveLinkPongMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	if (Message.PollRequest == CurrentPollRequest)
	{
		FScopeLock ScopedLock(&PollDataCriticalSection);
		
		const double MachineTimeOffset = LiveLinkMessageBusHelper::CalculateProviderMachineOffset(Message.CreationPlatformTime, Context);
		PollData.Add(FProviderPollResult(Context->GetSender(), Message.ProviderName, Message.MachineName, MachineTimeOffset));
	}
};

void ULiveLinkMessageBusFinder::ConnectToProvider(UPARAM(ref) FProviderPollResult& Provider, FLiveLinkSourceHandle& SourceHandle)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		FLiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<FLiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		TSharedPtr<FLiveLinkMessageBusSource> NewSource = MakeShared<FLiveLinkMessageBusSource>(FText::FromString(Provider.Name), FText::FromString(Provider.MachineName), Provider.Address, Provider.MachineTimeOffset);
		FGuid NewSourceGuid = LiveLinkClient->AddSource(NewSource);
		if (NewSourceGuid.IsValid())
		{
			if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(NewSourceGuid))
			{
				Settings->ConnectionString = ULiveLinkMessageBusSourceFactory::CreateConnectionString(Provider);
				Settings->Factory = ULiveLinkMessageBusSourceFactory::StaticClass();
			}
		}
		SourceHandle.SetSourcePointer(NewSource);
	}
	else
	{
		SourceHandle.SetSourcePointer(nullptr);
	}
};

ULiveLinkMessageBusFinder* ULiveLinkMessageBusFinder::ConstructMessageBusFinder()
{
	return NewObject<ULiveLinkMessageBusFinder>();
}


