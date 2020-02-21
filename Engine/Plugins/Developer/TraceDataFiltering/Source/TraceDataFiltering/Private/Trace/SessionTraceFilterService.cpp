// Copyright Epic Games, Inc. All Rights Reserved.

#include "SessionTraceFilterService.h"
#include "TraceServices/Model/Channel.h"
#include "TraceServices/ITraceServicesModule.h"
#include "Templates/SharedPointer.h"
#include "Misc/CoreDelegates.h"
#include "Algo/Transform.h"
#include "Modules/ModuleManager.h"
#include <Insights/IUnrealInsightsModule.h>
#include <Trace/StoreClient.h>
#include <Trace/ControlClient.h>
#include <IPAddress.h>
#include <SocketSubsystem.h>

FSessionTraceFilterService::FSessionTraceFilterService(Trace::FSessionHandle InHandle, TSharedPtr<const Trace::IAnalysisSession> InSession) : Session(InSession), Handle(InHandle)
{
	FCoreDelegates::OnEndFrame.AddRaw(this, &FSessionTraceFilterService::OnEndFrame);
	// Retrieve the channels currently enabled on the provider, this'll be the ones specified on the commandline (-trace=ChannelX)
	RetrieveAndStoreStartupChannels();
}

FSessionTraceFilterService::~FSessionTraceFilterService()
{
	FCoreDelegates::OnEndFrame.RemoveAll(this);
}

void FSessionTraceFilterService::GetRootObjects(TArray<FTraceObjectInfo>& OutObjects) const
{
	const Trace::IChannelProvider* ChannelProvider = Session->ReadProvider<Trace::IChannelProvider>("ChannelProvider");
	if (ChannelProvider)
	{
		const uint64 ChannelCount = ChannelProvider->GetChannelCount();		
		const TArray<Trace::FChannelEntry>& Channels = ChannelProvider->GetChannels();
		for (uint64 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
		{
			FTraceObjectInfo& EventInfo = OutObjects.AddDefaulted_GetRef();
			EventInfo.Name = Channels[ChannelIndex].Name;
			EventInfo.bEnabled = Channels[ChannelIndex].bIsEnabled;
			EventInfo.Hash = GetTypeHash(EventInfo.Name);
			EventInfo.OwnerHash = 0;
		}
	}
}

void FSessionTraceFilterService::GetChildObjects(uint32 InObjectHash, TArray<FTraceObjectInfo>& OutChildObjects) const
{
	/** TODO, parent/child relationship for Channels */
}

const FDateTime& FSessionTraceFilterService::GetTimestamp()
{
	const Trace::IChannelProvider* ChannelProvider = Session->ReadProvider<Trace::IChannelProvider>("ChannelProvider");
	if (ChannelProvider)
	{
		TimeStamp = ChannelProvider->GetTimeStamp();
	}

	return TimeStamp;
}

void FSessionTraceFilterService::SetObjectFilterState(const FString& InObjectName, const bool bFilterState)
{
	if (bFilterState)
	{
		FrameDisabledChannels.Remove(InObjectName);
		FrameEnabledChannels.Add(InObjectName);
	}
	else
	{
		FrameEnabledChannels.Remove(InObjectName);
		FrameDisabledChannels.Add(InObjectName);
	}
}

void FSessionTraceFilterService::UpdateFilterPresets(const TArray<TSharedPtr<IFilterPreset>>& InPresets)
{
	TSet<FString> UniqueNames;

	for (const TSharedPtr<IFilterPreset>& Preset : InPresets)
	{
		TArray<FString> Names;
		Preset->GetWhitelistedNames(Names);

		// We are only interested in the unique channels names, as a result of combining multiple presets
		Algo::Transform(Names, UniqueNames, [](FString InName) { return InName; });
	}

	DisableAllChannels();

	for (const FString& EventName : UniqueNames)
	{
		SetObjectFilterState(EventName, true);
	}

	for (const FString& EventName : FrameZeroEnabledChannels)
	{
		SetObjectFilterState(EventName, true);
	}
	// Commandline channels are only applied once when changing presets
	FrameZeroEnabledChannels.Empty();
}

void FSessionTraceFilterService::OnEndFrame()
{
	auto GenerateConcatenatedChannels = [](TSet<FString>& InChannels, FString& OutConcatenation)
	{
		for (const FString& ChannelName : InChannels)
		{
			OutConcatenation += ChannelName;
			OutConcatenation += TEXT(",");
		}
		OutConcatenation.RemoveFromEnd(TEXT(","));
	};

	if (FrameEnabledChannels.Num() == 0 && FrameDisabledChannels.Num() == 0)
	{
		return;
	}

	IUnrealInsightsModule& InsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	Trace::FStoreClient* StoreClient = InsightsModule.GetStoreClient();

	if (!StoreClient)
	{
		return;
	}

	const Trace::FStoreClient::FSessionInfo* SessionInfo = StoreClient->GetSessionInfoByTraceId(Handle);

	if (!SessionInfo)
	{
		return;
	}

	ISocketSubsystem* Sockets = ISocketSubsystem::Get();
	TSharedRef<FInternetAddr> ClientAddr(Sockets->CreateInternetAddr());
	ClientAddr->SetIp(SessionInfo->GetIpAddress());
	ClientAddr->SetPort(1985);

	Trace::FControlClient ControlClient;
	if (!ControlClient.Connect(ClientAddr.Get()))
	{
		return;
	}

	if (FrameEnabledChannels.Num())
	{
		FString EnabledChannels;
		GenerateConcatenatedChannels(FrameEnabledChannels, EnabledChannels);

		UE_LOG(LogTemp, Display, TEXT("CHANNELS %s: %d"), *EnabledChannels, true);
		ControlClient.SendToggleChannel(*EnabledChannels, true);
		
		FrameEnabledChannels.Empty();
	}

	if (FrameDisabledChannels.Num())
	{
		FString DisabledChannels;
		GenerateConcatenatedChannels(FrameDisabledChannels, DisabledChannels);
				
		UE_LOG(LogTemp, Display, TEXT("CHANNELS %s: %d"), *DisabledChannels, false);
		ControlClient.SendToggleChannel(*DisabledChannels, false);

		FrameDisabledChannels.Empty();
	}

	ControlClient.Disconnect();
}

void FSessionTraceFilterService::DisableAllChannels()
{
	const Trace::IChannelProvider* ChannelProvider = Session->ReadProvider<Trace::IChannelProvider>("ChannelProvider");
	if (ChannelProvider)
	{
		const uint64 ChannelCount = ChannelProvider->GetChannelCount();
		const TArray<Trace::FChannelEntry>& Channels = ChannelProvider->GetChannels();
		for (uint64 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
		{
			SetObjectFilterState(Channels[ChannelIndex].Name, false);
		}
	}
}

void FSessionTraceFilterService::RetrieveAndStoreStartupChannels()
{
	const Trace::IChannelProvider* ChannelProvider = Session->ReadProvider<Trace::IChannelProvider>("ChannelProvider");	
	if (ChannelProvider)
	{
		const uint64 ChannelCount = ChannelProvider->GetChannelCount();
		const TArray<Trace::FChannelEntry>& Channels = ChannelProvider->GetChannels();
		for (uint64 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
		{
			if (Channels[ChannelIndex].bIsEnabled)
			{
				FrameZeroEnabledChannels.Add(Channels[ChannelIndex].Name);
			}
		}
	}
}
