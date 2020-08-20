// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapPrivilegesModule.h"
#include "MagicLeapPrivilegeUtils.h"
#include "Engine/Engine.h"
#include "Stats/Stats.h"

using namespace MagicLeap;

DEFINE_LOG_CATEGORY(LogMagicLeapPrivileges);

FMagicLeapPrivilegesModule::FMagicLeapPrivilegesModule()
{}

void FMagicLeapPrivilegesModule::StartupModule()
{
	IMagicLeapPrivilegesModule::StartupModule();
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapPrivilegesModule::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);

	// Until there is a dedicated Lumin OnlineSubsystem, request the LAN privilege on module startup if the command line is requesting connection to a specified IP.
#if PLATFORM_LUMIN && !UE_SERVER
	const TCHAR* CmdLine = FCommandLine::Get();

	FString PackageName;
	if (FParse::Token(CmdLine, PackageName, 0))
	{
		FURL URL(nullptr, *PackageName, TRAVEL_Absolute);
		// No need to specially account for cook on the fly (filehostip/streaminghostip) because in that case the URL will still be local (not an explicit ip address),
		// so the privilege will not be requested, as expected.
		if (URL.Valid && !URL.HasOption(TEXT("failed")) && !URL.HasOption(TEXT("closed")) && !URL.HasOption(TEXT("restart")) &&
			!GDisallowNetworkTravel && !URL.HasOption(TEXT("listen")) && !URL.IsLocalInternal() && URL.IsInternal() && GIsClient)
		{
			RequestPrivilege(EMagicLeapPrivilege::LocalAreaNetwork);
		}
	}
#endif // PLATFORM_LUMIN && !UE_SERVER
}

void FMagicLeapPrivilegesModule::ShutdownModule()
{
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	IModuleInterface::ShutdownModule();
}

bool FMagicLeapPrivilegesModule::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMagicLeapPrivilegesModule_Tick);

#if WITH_MLSDK
	auto CopyPendingAsyncRequests(PendingAsyncRequests);

	PendingAsyncRequests.Empty();

	for (const auto& PendingAsyncRequest : CopyPendingAsyncRequests)
	{
		// Re-add pending ones
		MLResult Result = MLPrivilegesRequestPrivilegeTryGet(PendingAsyncRequest.Request);
		if (MLResult_Pending == Result)
		{
			PendingAsyncRequests.Add(PendingAsyncRequest);
			continue;
		}

		// Dispatch. Granted gets true, everything else gets false.
		if (MLPrivilegesResult_Granted == Result)
		{
			PendingAsyncRequest.StaticDelegate.ExecuteIfBound(PendingAsyncRequest.Privilege, true);
			PendingAsyncRequest.DynamicDelegate.ExecuteIfBound(PendingAsyncRequest.Privilege, true);
		}
		else
		{
			PendingAsyncRequest.StaticDelegate.ExecuteIfBound(PendingAsyncRequest.Privilege, false);
			PendingAsyncRequest.DynamicDelegate.ExecuteIfBound(PendingAsyncRequest.Privilege, false);
		}
	}
#endif //WITH_MLSDK
	return true;
}

bool FMagicLeapPrivilegesModule::CheckPrivilege(EMagicLeapPrivilege Privilege)
{
#if WITH_MLSDK
	auto Result = MLPrivilegesCheckPrivilege(MagicLeap::UnrealToMLPrivilege(Privilege));
	UE_CLOG(Result == MLResult_Ok, LogMagicLeapPrivileges, Error,
		TEXT("MLPrivilegesCheckPrivilege('%s') failed with error '%s'."),
		*MLPrivilegeToString(Privilege), UTF8_TO_TCHAR(MLPrivilegesGetResultString(Result)));
	if (MLPrivilegesResult_Granted == Result)
	{
		return true;
	}
#endif //WITH_MLSDK
	return false;
}

bool FMagicLeapPrivilegesModule::RequestPrivilege(EMagicLeapPrivilege Privilege)
{
#if WITH_MLSDK
	auto Result = MLPrivilegesRequestPrivilege(MagicLeap::UnrealToMLPrivilege(Privilege));
	UE_CLOG(Result == MLResult_Ok, LogMagicLeapPrivileges, Error,
		TEXT("MLPrivilegesRequestPrivilege('%s') failed with error '%s'."),
		*MLPrivilegeToString(Privilege), UTF8_TO_TCHAR(MLPrivilegesGetResultString(Result)));
	if (MLPrivilegesResult_Granted == Result)
	{
		return true;
	}
#endif //WITH_MLSDK
	return false;
}

bool FMagicLeapPrivilegesModule::RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestStaticDelegate& ResultDelegate)
{
	return RequestPrivilegeAsync(Privilege, ResultDelegate, FMagicLeapPrivilegeRequestDelegate());
}

bool FMagicLeapPrivilegesModule::RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestDelegate& ResultDelegate)
{
	return RequestPrivilegeAsync(Privilege, FMagicLeapPrivilegeRequestStaticDelegate(), ResultDelegate);
}

bool FMagicLeapPrivilegesModule::RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestStaticDelegate& StaticResultDelegate, const FMagicLeapPrivilegeRequestDelegate& DynamicResultDelegate)
{
#if WITH_MLSDK
	MLPrivilegesAsyncRequest* AsyncPrivilegeRequest = nullptr;

	auto Result = MLPrivilegesRequestPrivilegeAsync(MagicLeap::UnrealToMLPrivilege(Privilege), &AsyncPrivilegeRequest);

	if (MLResult_Ok != Result)
	{
		UE_LOG(LogMagicLeapPrivileges, Error,
			TEXT("MLPrivilegesRequestPrivilegeAsync('%s') failed with error '%s'."),
			*MLPrivilegeToString(Privilege), UTF8_TO_TCHAR(MLPrivilegesGetResultString(Result)));
		return false;
	}

	// Store the request
	PendingAsyncRequests.Add({ Privilege, AsyncPrivilegeRequest, StaticResultDelegate, DynamicResultDelegate });
	return true;
#else
	return false;
#endif //WITH_MLSDK
}


IMPLEMENT_MODULE(FMagicLeapPrivilegesModule, MagicLeapPrivileges);
