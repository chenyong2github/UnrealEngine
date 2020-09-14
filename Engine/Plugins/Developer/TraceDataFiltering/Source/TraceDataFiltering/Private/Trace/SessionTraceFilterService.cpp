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

FSessionTraceFilterService::FSessionTraceFilterService(Trace::FSessionHandle InHandle, TSharedPtr<const Trace::IAnalysisSession> InSession) : FBaseSessionFilterService(InHandle, InSession)
{

}

void FSessionTraceFilterService::OnApplyChannelChanges()
{
	auto GenerateConcatenatedChannels = [](TArray<FString>& InChannels, FString& OutConcatenation)
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
