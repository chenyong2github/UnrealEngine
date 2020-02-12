// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Containers/Queue.h"
#include "HAl/ThreadSafeBool.h"
#include "IMagicLeapConnectionsPlugin.h"
#include "Lumin/CAPIShims/LuminAPIConnections.h"
#include "MagicLeapConnectionsTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapConnections, Verbose, All);

class FMagicLeapConnectionsPlugin : public IMagicLeapConnectionsPlugin
{
public:
	FMagicLeapConnectionsPlugin();

	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime);
	bool EnableInvitesAsync(const FMagicLeapInviteReceivedDelegateMulti& InInviteReceivedDelegate);
	bool DisableInvites();
	bool IsInvitesEnabled() const;
	bool SendInviteAsync(const FMagicLeapConnectionsInviteArgs& UEArgs, FGuid& OutInviteHandle, const FMagicLeapInviteSentDelegateMulti& InInviteSentDelegate);
	bool CancelInvite(const FGuid& InviteRequestHandle);

private:
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	FThreadSafeBool bEnabled;
	FThreadSafeBool bEnabling;
#if WITH_MLSDK
	MLHandle ReceiveHandle;
	struct FSentInvite
	{
		MLHandle Handle;
		FMagicLeapInviteSentDelegateMulti Delegate;
	};
	TArray<FSentInvite> SentInvites;
#endif // WITH_MLSDK
	struct FReceivedInvite
	{
		bool bUserAccepted;
		FString Payload;
	};
	TQueue<FReceivedInvite, EQueueMode::Spsc> ReceivedInvites;
	FMagicLeapInviteReceivedDelegateMulti InviteReceivedDelegate;

#if WITH_MLSDK
	static MLConnectionsInviteeFilter UEToMLConnectionsInviteeFilter(EMagicLeapConnectionsInviteeFilter ConnectionsInviteeFilter);
	static EMagicLeapConnectionsInviteStatus MLToUEConnectionsInviteStatus(MLConnectionsInviteStatus ConnectionsInviteStatus);
	static void UEToMLConnectionsInviteArgs(const FMagicLeapConnectionsInviteArgs& UnrealArgs, MLConnectionsInviteArgs& MagicLeapArgs);
	static void OnRegistrationForReceivingInvitesComplete(MLResult Result, void* Context);
#endif // WITH_MLSDK
	static void OnInviteReceived(bool bUserAccepted, const char* Payload, void* Context);
};

inline FMagicLeapConnectionsPlugin& GetMagicLeapConnectionsPlugin()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapConnectionsPlugin>("MagicLeapConnections");
}
