// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapNetworkingPlugin.h"
#include "Async/Async.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY(LogMagicLeapNetworking);

FMagicLeapNetworkingPlugin::FMagicLeapNetworkingPlugin()
{}

void FMagicLeapNetworkingPlugin::StartupModule()
{
	IMagicLeapNetworkingPlugin::StartupModule();
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapNetworkingPlugin::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
}

void FMagicLeapNetworkingPlugin::ShutdownModule()
{
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	IMagicLeapNetworkingPlugin::ShutdownModule();
}

bool FMagicLeapNetworkingPlugin::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMagicLeapNetworkingPlugin_Tick);

	FConnectionQuery ConnectionQuery;
	if (ConnectionQueries.Peek(ConnectionQuery))
	{
		ConnectionQueries.Pop();
		ConnectionQuery.ResultDelegate.Broadcast(ConnectionQuery.bConnected);
	}

	FWifiDataQuery WifiDataQuery;
	if (WifiDataQueries.Peek(WifiDataQuery))
	{
		WifiDataQueries.Pop();
		WifiDataQuery.ResultDelegate.Broadcast(WifiDataQuery.WifiData);
	}

	return true;
}

bool FMagicLeapNetworkingPlugin::IsInternetConnectedAsync(const FInternetConnectionStatusDelegateMulti& ResultDelegate)
{
#if WITH_MLSDK
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, ResultDelegate]()
	{
		bool bIsConnected = false;
		MLResult Result = MLNetworkingIsInternetConnected(&bIsConnected);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapNetworking, Error, TEXT("MLNetworkingIsInternetConnected failed with error '%s'"), UTF8_TO_TCHAR(MLNetworkingGetResultString(Result)));
			return;
		}

		ConnectionQueries.Enqueue({ bIsConnected, ResultDelegate });
	});
	
	return true;
#else
	return false;
#endif // WITH_MLSDK
}

bool FMagicLeapNetworkingPlugin::GetWiFiDataAsync(const FWifiStatusDelegateMulti& ResultDelegate)
{
#if WITH_MLSDK
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, ResultDelegate]()
	{
		MLNetworkingWiFiData WifiData;
		MLNetworkingWiFiDataInit(&WifiData);
		MLResult Result = MLNetworkingGetWiFiData(&WifiData);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapNetworking, Error, TEXT("MLNetworkingGetWiFiData failed with error '%s'"), UTF8_TO_TCHAR(MLNetworkingGetResultString(Result)));
			return;
		}

		FMagicLeapNetworkingWiFiData UENetworkWifiData;
		MLToUENetworkingWifiData(WifiData, UENetworkWifiData);
		WifiDataQueries.Enqueue({ UENetworkWifiData, ResultDelegate });
	});


	return true;
#else
	return false;
#endif // WITH_MLSDK
}

#if WITH_MLSDK
void FMagicLeapNetworkingPlugin::MLToUENetworkingWifiData(const  MLNetworkingWiFiData& InMLNetworkingWiFiData, FMagicLeapNetworkingWiFiData& OutUENetworkingWiFiData)
{
	OutUENetworkingWiFiData.RSSI = InMLNetworkingWiFiData.rssi;
	OutUENetworkingWiFiData.Linkspeed = InMLNetworkingWiFiData.linkspeed;
	OutUENetworkingWiFiData.Frequency = InMLNetworkingWiFiData.frequency;
}
#endif // WITH_MLSDK

IMPLEMENT_MODULE(FMagicLeapNetworkingPlugin, MagicLeapNetworking);
