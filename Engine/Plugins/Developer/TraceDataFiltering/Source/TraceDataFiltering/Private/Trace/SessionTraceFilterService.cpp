// Copyright Epic Games, Inc. All Rights Reserved.

#include "SessionTraceFilterService.h"
#include "TraceServices/Model/Channel.h"
#include "TraceServices/ITraceServicesModule.h"

FSessionTraceFilterService::FSessionTraceFilterService(Trace::FSessionHandle InHandle, TSharedPtr<const Trace::IAnalysisSession> InSession) : Session(InSession), Handle(InHandle)
{
	FCoreDelegates::OnEndFrame.AddRaw(this, &FSessionTraceFilterService::OnEndFrame);
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
}

void FSessionTraceFilterService::OnEndFrame()
{
	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	TSharedPtr<Trace::ISessionService> SessionService = TraceServicesModule.GetSessionService();

	auto GenerateConcatenatedChannels = [](TSet<FString>& InChannels, FString& OutConcatenation)
	{
		for (const FString& ChannelName : InChannels)
		{
			OutConcatenation += ChannelName;
			OutConcatenation += TEXT(",");
		}
		OutConcatenation.RemoveFromEnd(TEXT(","));
	};

	if (FrameEnabledChannels.Num())
	{
		FString EnabledChannels;
		GenerateConcatenatedChannels(FrameEnabledChannels, EnabledChannels);

		if (SessionService.IsValid())
		{
			UE_LOG(LogTemp, Display, TEXT("CHANNELS %s: %d"), *EnabledChannels, true);
			SessionService->ToggleChannels(Handle, *EnabledChannels, true);
		}

		FrameEnabledChannels.Empty();
	}

	if (FrameDisabledChannels.Num())
	{
		FString DisabledChannels;
		GenerateConcatenatedChannels(FrameDisabledChannels, DisabledChannels);
				
		if (SessionService.IsValid())
		{
			UE_LOG(LogTemp, Display, TEXT("CHANNELS %s: %d"), *DisabledChannels, false);
			SessionService->ToggleChannels(Handle, *DisabledChannels, false);
		}

		FrameDisabledChannels.Empty();
	}
}

void FSessionTraceFilterService::DisableAllChannels()
{
	const Trace::IChannelProvider* ChannelProvider = Session->ReadProvider<Trace::IChannelProvider>("ChannelProvider");
	if (ChannelProvider)
	{
		const uint64 ChannelCount = ChannelProvider->GetChannelCount();
		auto Channels = ChannelProvider->GetChannels();
		for (uint64 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
		{
			SetObjectFilterState(Channels[ChannelIndex].Name, false);
		}
	}
}
