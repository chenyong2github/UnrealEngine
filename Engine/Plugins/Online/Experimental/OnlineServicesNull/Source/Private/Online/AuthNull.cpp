// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/AuthNull.h"

#include "Online/OnlineServicesNull.h"
#include "Online/OnlineServicesNullTypes.h"
#include "Online/AuthErrors.h"
#include "Online/OnlineErrorDefinitions.h"

namespace UE::Online {
// Copied from OSS Null

#define NULL_OSS_STRING_BUFFER_LENGTH 256
#define NULL_MAX_TOKEN_SIZE 4096


FAuthNull::FAuthNull(FOnlineServicesNull& InServices)
	: FAuthCommon(InServices)
{
}

void FAuthNull::Initialize()
{
	FAuthCommon::Initialize();

}

void FAuthNull::PreShutdown()
{
}

FString FAuthNull::GenerateRandomUserId(int32 LocalUserNum)
{
	FString HostName;
	/*if(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		if (!ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetHostName(HostName))
		{
			// could not get hostname, use address
			bool bCanBindAll;
			TSharedPtr<class FInternetAddr> Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalHostAddr(*GLog, bCanBindAll);
			HostName = Addr->ToString(false);
		}
	}*/

	bool bUseStableNullId = false;
	FString UserSuffix;

	if (true)
	{
		UserSuffix = FString::Printf(TEXT("-%d"), LocalUserNum);
	}
	 
#if !(UE_BUILD_SHIPPING && WITH_EDITOR)
	if (GIsFirstInstance && !GIsEditor)
	{
		// If we're outside the editor and know this is the first instance, use the system login id
		bUseStableNullId = true;
	}
#endif

	if (bUseStableNullId)
	{
		// Use a stable id possibly with a user num suffix
		return FString::Printf(TEXT("OSSV2-%s-%s%s"), *HostName, *FPlatformMisc::GetLoginId().ToUpper(), *UserSuffix);
	}

	// If we're not the first instance (or in the editor), return truly random id
	return FString::Printf(TEXT("OSSV2-%s-%s%s"), *HostName, *FGuid::NewGuid().ToString(), *UserSuffix);
}

TOnlineAsyncOpHandle<FAuthLogin> FAuthNull::Login(FAuthLogin::Params&& Params)
{
	TOnlineAsyncOpRef<FAuthLogin> Op = GetOp<FAuthLogin>(MoveTemp(Params));
	int32 LocalUserNum = FPlatformMisc::GetUserIndexForPlatformUser(Op->GetParams().PlatformUserId);

	Op->Then([this, LocalUserNum](TOnlineAsyncOp<FAuthLogin>& InAsyncOp) mutable
	{
		TSharedRef<FAccountInfoNull> AccountInfo = MakeShared<FAccountInfoNull>();
		FString DisplayId = GenerateRandomUserId(LocalUserNum);
		AccountInfo->PlatformUserId = InAsyncOp.GetParams().PlatformUserId;
		AccountInfo->DisplayName = DisplayId;
		AccountInfo->UserId = FOnlineAccountIdRegistryNull::Get().Create(DisplayId, AccountInfo->PlatformUserId);
		AccountInfo->LoginStatus = ELoginStatus::LoggedIn;
		FAuthLogin::Result Result{ AccountInfo };
		AccountInfos.Add(AccountInfo->UserId, AccountInfo);
		InAsyncOp.SetResult(MoveTemp(Result));
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FAuthLogout> FAuthNull::Logout(FAuthLogout::Params&& Params)
{
	// todo: login status change delegates
	TOnlineAsyncOpRef<FAuthLogout> Op = GetOp<FAuthLogout>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FAuthLogout>& InAsyncOp) mutable
	{
		auto Result = GetAccountByAccountId({InAsyncOp.GetParams().LocalUserId});
		if(Result.IsOk())
		{
			AccountInfos.Remove(Result.GetOkValue().AccountInfo->UserId);
			InAsyncOp.SetResult(FAuthLogout::Result());
		}
		else
		{
			InAsyncOp.SetError(Errors::Unknown());
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineResult<FAuthGetAccountByAccountId> FAuthNull::GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params)
{
	if (TSharedRef<FAccountInfoNull>* const FoundAccount = AccountInfos.Find(Params.LocalUserId))
	{
		return TOnlineResult<FAuthGetAccountByAccountId>({*FoundAccount});
	}
	else
	{
		// TODO: proper error
		return TOnlineResult<FAuthGetAccountByAccountId>(Errors::Unknown());
	}
}

bool FAuthNull::IsLoggedIn(const FOnlineAccountIdHandle& AccountId) const
{
	// TODO:  More logic?
	return AccountInfos.Contains(AccountId);
}

TResult<FOnlineAccountIdHandle, FOnlineError> FAuthNull::GetAccountIdByLocalUserNum(int32 LocalUserNum) const
{
	FPlatformUserId UserId = FPlatformMisc::GetPlatformUserForUserIndex(LocalUserNum);
	for (const TPair<FOnlineAccountIdHandle, TSharedRef<FAccountInfoNull>>& AccountPair : AccountInfos)
	{
		if (AccountPair.Value->PlatformUserId == UserId)
		{
			TResult<FOnlineAccountIdHandle, FOnlineError> Result(AccountPair.Key);
			return Result;
		}
	}
	TResult<FOnlineAccountIdHandle, FOnlineError> Result(Errors::Unknown()); // TODO: error code
	return Result;
}

TOnlineResult<FAuthGetAccountByPlatformUserId> FAuthNull::GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params)
{
	for (const TPair<FOnlineAccountIdHandle, TSharedRef<FAccountInfoNull>>& AccountPair : AccountInfos)
	{
		if (AccountPair.Value->PlatformUserId == Params.PlatformUserId)
		{
			return TOnlineResult<FAuthGetAccountByPlatformUserId>({AccountPair.Value});
		}
	}
	TOnlineResult<FAuthGetAccountByPlatformUserId> Result(Errors::Unknown()); // TODO: error code
	return Result;
}

// FOnlineAccountIdRegistryNull
FOnlineAccountIdRegistryNull& FOnlineAccountIdRegistryNull::Get()
{
	static FOnlineAccountIdRegistryNull Instance;
	return Instance;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryNull::Find(FString UserId) const
{
	const FOnlineAccountIdString* Entry = StringToId.Find(UserId);
	if(Entry)
	{
		return Entry->Handle;
	}
	return FOnlineAccountIdHandle();
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryNull::Find(FPlatformUserId UserId) const
{
	const FOnlineAccountIdString* Entry = LocalUserMap.Find(UserId.GetInternalId());
	if (Entry)
	{
		return Entry->Handle;
	}
	return FOnlineAccountIdHandle();
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryNull::Find(int32 UserId) const
{
	const FOnlineAccountIdString* Entry = LocalUserMap.Find(UserId);
	if (Entry)
	{
		return Entry->Handle;
	}
	return FOnlineAccountIdHandle();
}

const FOnlineAccountIdString* FOnlineAccountIdRegistryNull::GetInternal(const FOnlineAccountIdHandle& Handle) const
{
	if(Handle.IsValid() && Handle.GetOnlineServicesType() == EOnlineServices::Null && Handle.GetHandle() <= (uint32)Ids.Num())
	{
		return &Ids[Handle.GetHandle()-1];
	}
	return nullptr;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryNull::Create(FString UserId, FPlatformUserId LocalUserIndex/* = FPlatformUserId::PLATFORMUSERID_NONE*/)
{
	FOnlineAccountIdHandle ExistingHandle = Find(UserId);
	if(ExistingHandle.IsValid())
	{
		return ExistingHandle;
	}

	FOnlineAccountIdString& Id = Ids.Emplace_GetRef();
	Id.AccountIndex = Ids.Num();
	Id.Data = UserId;
	Id.Handle = FOnlineAccountIdHandle(EOnlineServices::Null, Id.AccountIndex);
	
	StringToId.Add(UserId, Id);
	if(LocalUserIndex.IsValid())
	{
		if(LocalUserMap.Contains(LocalUserIndex.GetInternalId()))
		{
			UE_LOG(LogTemp, Error, TEXT("OssNull: Found a duplicate ID for local user %d"), LocalUserIndex.GetInternalId());
		}
		LocalUserMap.Add(LocalUserIndex.GetInternalId(), Id);
	}

	return Id.Handle;
}

FString FOnlineAccountIdRegistryNull::ToLogString(const FOnlineAccountIdHandle& Handle) const
{
	if(const FOnlineAccountIdString* Id = GetInternal(Handle))
	{
		return Id->Data;
	}

	return FString(TEXT("[InvalidNetID]"));
}


TArray<uint8> FOnlineAccountIdRegistryNull::ToReplicationData(const FOnlineAccountIdHandle& Handle) const
{
	if (const FOnlineAccountIdString* Id = GetInternal(Handle))
	{
		TArray<uint8> ReplicationData;
		ReplicationData.SetNumUninitialized(Id->Data.Len());
		StringToBytes(Id->Data, ReplicationData.GetData(), Id->Data.Len());
		UE_LOG(LogTemp, VeryVerbose, TEXT("StringToBytes on %s returned %d len"), *Id->Data, ReplicationData.Num())
		return ReplicationData;
	}

	return TArray<uint8>();;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryNull::FromReplicationData(const TArray<uint8>& ReplicationData)
{
	FString Result = BytesToString(ReplicationData.GetData(), ReplicationData.Num());
	if(Result.Len() > 0)
	{
		return Create(Result);
	}
	return FOnlineAccountIdHandle();
}

/* UE::Online */ }
