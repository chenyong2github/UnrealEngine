// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "IMagicLeapPlugin.h"
#include "IMagicLeapNetworkingPlugin.h"
#include "MagicLeapNetworkingTypes.h"
#include "Lumin/CAPIShims/LuminAPINetworking.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapNetworking, Verbose, All);

class FMagicLeapNetworkingPlugin : public IMagicLeapNetworkingPlugin
{
public:
	FMagicLeapNetworkingPlugin();
	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime);
	bool IsInternetConnectedAsync(const FInternetConnectionStatusDelegateMulti& ResultDelegate);
	bool GetWiFiDataAsync(const FWifiStatusDelegateMulti& ResultDelegate);

private:
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	struct FConnectionQuery
	{
		bool bConnected;
		FInternetConnectionStatusDelegateMulti ResultDelegate;
	};
	TQueue<FConnectionQuery, EQueueMode::Spsc> ConnectionQueries;
	struct FWifiDataQuery
	{
		FMagicLeapNetworkingWiFiData WifiData;
		FWifiStatusDelegateMulti ResultDelegate;
	};
	TQueue<FWifiDataQuery, EQueueMode::Spsc> WifiDataQueries;

#if WITH_MLSDK
	void MLToUENetworkingWifiData(const MLNetworkingWiFiData& InMLNetworkingWiFiData, FMagicLeapNetworkingWiFiData& OutUENetworkingWiFiData);
#endif // WITH_MLSDK
};

inline FMagicLeapNetworkingPlugin& GetMagicLeapNetworkingPlugin()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapNetworkingPlugin>("MagicLeapNetworking");
}
