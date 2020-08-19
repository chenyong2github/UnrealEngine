// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapConnectionsPlugin.h"
#include "MagicLeapHandle.h"
#include "Async/Async.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY(LogMagicLeapConnections);

using namespace MagicLeap;

FMagicLeapConnectionsPlugin::FMagicLeapConnectionsPlugin()
: bEnabled(false)
, bEnabling(false)
#if WITH_MLSDK
, ReceiveHandle(ML_INVALID_HANDLE)
#endif // WITH_MLSDK
{
}

void FMagicLeapConnectionsPlugin::StartupModule()
{
	IMagicLeapConnectionsPlugin::StartupModule();
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapConnectionsPlugin::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
}

void FMagicLeapConnectionsPlugin::ShutdownModule()
{
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	DisableInvites();
	IMagicLeapConnectionsPlugin::ShutdownModule();
}

bool FMagicLeapConnectionsPlugin::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMagicLeapConnectionsPlugin_Tick);

	(void)DeltaTime;
	if (!bEnabled)
	{
		return true;
	}

	FReceivedInvite ReceivedInvite;
	if (ReceivedInvites.Dequeue(ReceivedInvite) && InviteReceivedDelegate.IsBound()) // process one per frame
	{
		InviteReceivedDelegate.Broadcast(ReceivedInvite.bUserAccepted, ReceivedInvite.Payload);
	}

#if WITH_MLSDK
	MLConnectionsInviteStatus InviteStatus = MLConnectionsInviteStatus::MLConnectionsInviteStatus_Ensure32Bits;
	for (int32 iSentInvite = SentInvites.Num()-1; iSentInvite > -1; --iSentInvite)
	{
		MLHandle InviteHandle = SentInvites[iSentInvite].Handle;
		MLResult Result = MLConnectionsTryGetInviteStatus(InviteHandle, &InviteStatus);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapConnections, Error, TEXT("MLConnectionsTryGetInviteStatus failed with error '%s'"), UTF8_TO_TCHAR(MLConnectionsGetResultString(Result)));
			Result = MLConnectionsReleaseRequestResources(InviteHandle);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapConnections, Error, TEXT("MLConnectionsReleaseRequestResources failed with error '%s'"), UTF8_TO_TCHAR(MLConnectionsGetResultString(Result)));
			SentInvites.RemoveAt(iSentInvite);
			continue;
		}

		switch (InviteStatus)
		{
		case MLConnectionsInviteStatus_SubmittingRequest:
		case MLConnectionsInviteStatus_Pending:
		{
			continue;
		}
		break;
		case MLConnectionsInviteStatus_Dispatched:
		case MLConnectionsInviteStatus_DispatchFailed:
		case MLConnectionsInviteStatus_Cancelled:
		case MLConnectionsInviteStatus_InvalidHandle:
		{
			SentInvites[iSentInvite].Delegate.Broadcast(MLToUEConnectionsInviteStatus(InviteStatus), MLHandleToFGuid(InviteHandle));
			Result = MLConnectionsReleaseRequestResources(InviteHandle);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapConnections, Error, TEXT("MLConnectionsReleaseRequestResources failed with error '%s'"), UTF8_TO_TCHAR(MLConnectionsGetResultString(Result)));
			SentInvites.RemoveAt(iSentInvite);
		}
		break;
		case MLConnectionsInviteStatus_Ensure32Bits:
		{
			UE_LOG(LogMagicLeapConnections, Error, TEXT("Unexpected connection invite status 'MLConnectionsInviteStatus_Ensure32Bits' encountered!"));
		}
		break;
		}
	}
#endif // WITH_MLSDK
	return true;
}

bool FMagicLeapConnectionsPlugin::EnableInvitesAsync(const FMagicLeapInviteReceivedDelegateMulti& InInviteReceivedDelegate)
{
#if WITH_MLSDK
	InviteReceivedDelegate = InInviteReceivedDelegate;
	if (bEnabling)
	{
		UE_LOG(LogMagicLeapConnections, Log, TEXT("Invites are currently being enabled.  Ignoring enable request."));
		return false;
	}

	if (bEnabled)
	{
		UE_LOG(LogMagicLeapConnections, Log, TEXT("Invites are currently enabled. Ingoring enable request."));
		return false;
	}

	bEnabling = true;
	// the following api functions must be called together in order for any invitation to be sent or received
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]()
	{
		MLResult Result = MLConnectionsRegistrationStartup(&ReceiveHandle);
		if (Result != MLResult_Ok)
		{
			ReceiveHandle = ML_INVALID_HANDLE;
			UE_LOG(LogMagicLeapConnections, Error, TEXT("MLConnectionsRegistrationStartup failed with error '%s'"), UTF8_TO_TCHAR(MLConnectionsGetResultString(Result)));
			bEnabling = false;
			return;
		}

		Result = MLConnectionsStartup();
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapConnections, Error, TEXT("MLConnectionsStartup failed with error '%s'"), UTF8_TO_TCHAR(MLConnectionsGetResultString(Result)));
			bEnabling = false;
			return;
		}

		MLConnectionsInviteCallbacks Callbacks;
		Callbacks.on_registration_complete = OnRegistrationForReceivingInvitesComplete;
		Callbacks.on_invitation = OnInviteReceived;
		Result = MLConnectionsRegisterForInvite(ReceiveHandle, Callbacks, this);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapConnections, Error, TEXT("MLConnectionsRegisterForInvite failed with error '%s'"), UTF8_TO_TCHAR(MLConnectionsGetResultString(Result)));
			bEnabling = false;
			return;
		}

		bEnabled = true;
		bEnabling = false;
	});
#endif // WITH_MLSDK
	return true;
}

bool FMagicLeapConnectionsPlugin::DisableInvites()
{
	if (bEnabling)
	{
		UE_LOG(LogMagicLeapConnections, Log, TEXT("Invites are currenlty being enabled.  Ingoring disable request."));
		return false;
	}

	if (!bEnabled)
	{
		UE_LOG(LogMagicLeapConnections, Log, TEXT("Invites are currenlty disabled.  Ingoring disable request."));
		return false;
	}

#if WITH_MLSDK
	if (MLHandleIsValid(ReceiveHandle))
	{
		MLResult Result = MLConnectionsRegistrationShutdown(ReceiveHandle);
		ReceiveHandle = ML_INVALID_HANDLE;
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapConnections, Error, TEXT("MLConnectionsRegistrationShutdown failed with error '%s'"), UTF8_TO_TCHAR(MLConnectionsGetResultString(Result)));
		Result = MLConnectionsShutdown();
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapConnections, Error, TEXT("MLConnectionsShutdown failed with error '%s'"), UTF8_TO_TCHAR(MLConnectionsGetResultString(Result)));
	}
#endif // WITH_MLSDK

	bEnabled = false;
	return true;
}

bool FMagicLeapConnectionsPlugin::IsInvitesEnabled() const
{
	return bEnabled;
}

bool FMagicLeapConnectionsPlugin::SendInviteAsync(const FMagicLeapConnectionsInviteArgs& UEArgs, FGuid& OutInviteHandle, const FMagicLeapInviteSentDelegateMulti& InInviteSentDelegate)
{
#if WITH_MLSDK
	MLHandle InviteHandle = ML_INVALID_HANDLE;
	MLConnectionsInviteArgs MLArgs;
	UEToMLConnectionsInviteArgs(UEArgs, MLArgs);
	MLResult Result = MLConnectionsRequestInvite(&MLArgs, &InviteHandle);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapConnections, Error, TEXT("MLConnectionsRequestInvite failed with error '%s'"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		return false;
	}

	SentInvites.Add({ InviteHandle, InInviteSentDelegate });
	OutInviteHandle = MLHandleToFGuid(InviteHandle);
	return true;
#else
	return false;
#endif // WITH_MLSDK
}

bool FMagicLeapConnectionsPlugin::CancelInvite(const FGuid& InviteRequestHandle)
{
#if WITH_MLSDK
	for (int32 iSentInvite = 0; iSentInvite < SentInvites.Num(); ++iSentInvite)
	{
		if (MLHandleToFGuid(SentInvites[iSentInvite].Handle) == InviteRequestHandle)
		{
			MLResult Result = MLConnectionsCancelInvite(SentInvites[iSentInvite].Handle);
			UE_CLOG(MLResult_Ok != Result, LogMagicLeapConnections, Error, TEXT("MLConnectionsCancelInvite failed with error '%s'"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			SentInvites.RemoveAt(iSentInvite);
			return true;
		}
	}
#endif // WITH_MLSDK
	return false;
}

#if WITH_MLSDK
MLConnectionsInviteeFilter FMagicLeapConnectionsPlugin::UEToMLConnectionsInviteeFilter(EMagicLeapConnectionsInviteeFilter InConnectionsInviteeFilter)
{
	return MLConnectionsInviteeFilter_Followers;
	//JMC TODO: revisit once other filters are functional.
	/*MLConnectionsInviteeFilter ConnectionsInviteeFilter = MLConnectionsInviteeFilter_Ensure32Bits;
	switch (InConnectionsInviteeFilter)
	{
	case EMagicLeapConnectionsInviteeFilter::Following: ConnectionsInviteeFilter = MLConnectionsInviteeFilter_Following; break;
	case EMagicLeapConnectionsInviteeFilter::Followers: ConnectionsInviteeFilter = MLConnectionsInviteeFilter_Followers; break;
	case EMagicLeapConnectionsInviteeFilter::Mutual: ConnectionsInviteeFilter = MLConnectionsInviteeFilter_Mutual; break;
	}

	return ConnectionsInviteeFilter;*/
}

EMagicLeapConnectionsInviteStatus FMagicLeapConnectionsPlugin::MLToUEConnectionsInviteStatus(MLConnectionsInviteStatus InConnectionsInviteStatus)
{
	EMagicLeapConnectionsInviteStatus ConnectionsInviteStatus = EMagicLeapConnectionsInviteStatus::InvalidHandle;
	switch (InConnectionsInviteStatus)
	{
	case MLConnectionsInviteStatus_SubmittingRequest: ConnectionsInviteStatus = EMagicLeapConnectionsInviteStatus::SubmittingRequest; break;
	case MLConnectionsInviteStatus_Pending: ConnectionsInviteStatus = EMagicLeapConnectionsInviteStatus::Pending; break;
	case MLConnectionsInviteStatus_Dispatched: ConnectionsInviteStatus = EMagicLeapConnectionsInviteStatus::Dispatched; break;
	case MLConnectionsInviteStatus_DispatchFailed: ConnectionsInviteStatus = EMagicLeapConnectionsInviteStatus::DispatchFailed; break;
	case MLConnectionsInviteStatus_Cancelled: ConnectionsInviteStatus = EMagicLeapConnectionsInviteStatus::Cancelled; break;
	case MLConnectionsInviteStatus_InvalidHandle: ConnectionsInviteStatus = EMagicLeapConnectionsInviteStatus::InvalidHandle; break;		
	case MLConnectionsInviteStatus_Ensure32Bits:
	{
		UE_LOG(LogMagicLeapConnections, Error, TEXT("Unexpected connection invite status 'MLConnectionsInviteStatus_Ensure32Bits' encountered!"));
	}
	break;
	}
	
	return ConnectionsInviteStatus;
}

void FMagicLeapConnectionsPlugin::UEToMLConnectionsInviteArgs(const FMagicLeapConnectionsInviteArgs& UnrealArgs, MLConnectionsInviteArgs& MagicLeapArgs)
{
	MLConnectionsInviteArgsInit(&MagicLeapArgs);
	MagicLeapArgs.invitee_count = UnrealArgs.InviteeCount;
	MagicLeapArgs.invite_user_prompt = static_cast<const char*>(FMemory::Malloc(UnrealArgs.InviteUserPrompt.Len() + 1));
	FMemory::Memzero((void*)MagicLeapArgs.invite_user_prompt, UnrealArgs.InviteUserPrompt.Len() + 1);
	FMemory::Memcpy((void*)MagicLeapArgs.invite_user_prompt, TCHAR_TO_UTF8(*UnrealArgs.InviteUserPrompt), UnrealArgs.InviteUserPrompt.Len());
	MagicLeapArgs.invite_payload = static_cast<const char*>(FMemory::Malloc(UnrealArgs.InvitePayload.Len() + 1));
	FMemory::Memzero((void*)MagicLeapArgs.invite_payload, UnrealArgs.InvitePayload.Len() + 1);
	FMemory::Memcpy((void*)MagicLeapArgs.invite_payload, TCHAR_TO_UTF8(*UnrealArgs.InvitePayload), UnrealArgs.InvitePayload.Len());
	MagicLeapArgs.default_invitee_filter = UEToMLConnectionsInviteeFilter(UnrealArgs.DefaultInviteeFilter);
}

void FMagicLeapConnectionsPlugin::OnRegistrationForReceivingInvitesComplete(MLResult Result, void* Context)
{
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapConnections, Error, TEXT("Registering for receiving invites failed with error '%s'"), UTF8_TO_TCHAR(MLConnectionsGetResultString(Result)));
}
#endif // WITH_MLSDK

void FMagicLeapConnectionsPlugin::OnInviteReceived(bool bUserAccepted, const char* Payload, void* Context)
{
	FMagicLeapConnectionsPlugin* This = static_cast<FMagicLeapConnectionsPlugin*>(Context);
	This->ReceivedInvites.Enqueue({ bUserAccepted, UTF8_TO_TCHAR(Payload) });
}

IMPLEMENT_MODULE(FMagicLeapConnectionsPlugin, MagicLeapConnections);
