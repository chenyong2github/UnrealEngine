// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/AuthNull.h"

#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "Misc/CoreDelegates.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Online/OnlineServicesNull.h"
#include "Online/OnlineServicesNullTypes.h"
#include "Online/AuthErrors.h"
#include "Online/OnlineErrorDefinitions.h"
#include "SocketSubsystem.h"

#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

namespace UE::Online {
// Copied from OSS Null

struct FAuthNullConfig
{
	bool bAddUserNumToNullId = false;
	bool bForceStableNullId = false;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FAuthNullConfig)
	ONLINE_STRUCT_FIELD(FAuthNullConfig, bAddUserNumToNullId),
	ONLINE_STRUCT_FIELD(FAuthNullConfig, bForceStableNullId)
END_ONLINE_STRUCT_META()

/* Meta*/ }

namespace {

FString GenerateRandomUserId(const FAuthNullConfig& Config, FPlatformUserId PlatformUserId)
{
	FString HostName;
	if(ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		if (!SocketSubsystem->GetHostName(HostName))
		{
			// could not get hostname, use address
			bool bCanBindAll;
			TSharedPtr<class FInternetAddr> Addr = SocketSubsystem->GetLocalHostAddr(*GLog, bCanBindAll);
			HostName = Addr->ToString(false);
		}
	}

	bool bUseStableNullId = Config.bForceStableNullId;
	FString UserSuffix;

	if (Config.bAddUserNumToNullId)
	{
		UserSuffix = FString::Printf(TEXT("-%d"), PlatformUserId.GetInternalId());
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

TSharedRef<FAccountInfoNull> CreateAccountInfo(const FAuthNullConfig& Config, FPlatformUserId PlatformUserId)
{
	const FString DisplayId = GenerateRandomUserId(Config, PlatformUserId);
	return MakeShared<FAccountInfoNull>(FAccountInfoNull{
		FOnlineAccountIdRegistryNull::Get().Create(DisplayId, PlatformUserId),
		PlatformUserId,
		ELoginStatus::LoggedIn,
		{ { AccountAttributeData::DisplayName, DisplayId } }
		});
}

/* anonymous*/ }

TSharedPtr<FAccountInfoNull> FAccountInfoRegistryNULL::Find(FPlatformUserId PlatformUserId) const
{
	return StaticCastSharedPtr<FAccountInfoNull>(Super::Find(PlatformUserId));
}

TSharedPtr<FAccountInfoNull> FAccountInfoRegistryNULL::Find(FOnlineAccountIdHandle AccountIdHandle) const
{
	return StaticCastSharedPtr<FAccountInfoNull>(Super::Find(AccountIdHandle));
}

void FAccountInfoRegistryNULL::Register(const TSharedRef<FAccountInfoNull>& AccountInfoNULL)
{
	DoRegister(AccountInfoNULL);
}

void FAccountInfoRegistryNULL::Unregister(FOnlineAccountIdHandle AccountId)
{
	if (TSharedPtr<FAccountInfoNull> AccountInfoNULL = Find(AccountId))
	{
		DoUnregister(AccountInfoNULL.ToSharedRef());
	}
	else
	{
		UE_LOG(LogOnlineServices, Warning, TEXT("[FAccountInfoRegistryNULL::Unregister] Failed to find account [%s]."), *ToLogString(AccountId));
	}
}

FAuthNull::FAuthNull(FOnlineServicesNull& InServices)
	: FAuthCommon(InServices)
{
}

void FAuthNull::Initialize()
{
	FAuthCommon::Initialize();
	InitializeUsers();
}

void FAuthNull::PreShutdown()
{
	FAuthCommon::PreShutdown();
	UninitializeUsers();
}

const FAccountInfoRegistry& FAuthNull::GetAccountInfoRegistry() const
{
	return AccountInfoRegistryNULL;
}

void FAuthNull::InitializeUsers()
{
	FAuthNullConfig AuthNullConfig;
	LoadConfig(AuthNullConfig);

	// There is no "login" for Null - all local users are initialized as "logged in".
	TArray<FPlatformUserId> Users;
	IPlatformInputDeviceMapper::Get().GetAllActiveUsers(Users);
	Algo::ForEach(Users, [&](FPlatformUserId PlatformUserId)
	{
		AccountInfoRegistryNULL.Register(CreateAccountInfo(AuthNullConfig, PlatformUserId));
	});

	// Setup hook to add new users when they become available.
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddRaw(this, &FAuthNull::OnInputDeviceConnectionChange);
}

void FAuthNull::UninitializeUsers()
{
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().RemoveAll(this);
}

void FAuthNull::OnInputDeviceConnectionChange(EInputDeviceConnectionState NewConnectionState, FPlatformUserId PlatformUserId, FInputDeviceId InputDeviceId)
{
	// If this is a new platform user then register an entry for them so they will be seen as "logged-in".
	if (!AccountInfoRegistryNULL.Find(PlatformUserId))
	{
		FAuthNullConfig AuthNullConfig;
		LoadConfig(AuthNullConfig);

		TSharedRef<FAccountInfoNull> AccountInfo = CreateAccountInfo(AuthNullConfig, PlatformUserId);
		AccountInfoRegistryNULL.Register(AccountInfo);
		OnAuthLoginStatusChangedEvent.Broadcast(FAuthLoginStatusChanged{ AccountInfo, ELoginStatus::LoggedIn });
	}
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
	const FOnlineAccountIdString* Entry = LocalUserMap.Find(UserId);
	if (Entry)
	{
		return Entry->Handle;
	}
	return FOnlineAccountIdHandle();
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryNull::Find(int32 UserIndex) const
{
	const FOnlineAccountIdString* Entry = LocalUserMap.Find(FPlatformMisc::GetPlatformUserForUserIndex(UserIndex));
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

FOnlineAccountIdHandle FOnlineAccountIdRegistryNull::Create(FString UserId, FPlatformUserId PlatformUserId/* = PLATFORMUSERID_NONE*/)
{
	FOnlineAccountIdHandle ExistingHandle = Find(UserId);
	if(ExistingHandle.IsValid())
	{
		UE_LOG(LogOnlineServices, Error, TEXT("[FOnlineAccountIdRegistryNull::Create] Found a duplicate ID for local user %d."), FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId));
		return ExistingHandle;
	}

	if (!PlatformUserId.IsValid())
	{
		UE_LOG(LogOnlineServices, Warning, TEXT("[FOnlineAccountIdRegistryNull::Create] Unable to create id: PlatformUserId is invalid."));
		return FOnlineAccountIdHandle();
	}

	FOnlineAccountIdString& Id = Ids.Emplace_GetRef();
	Id.AccountIndex = Ids.Num();
	Id.Data = UserId;
	Id.Handle = FOnlineAccountIdHandle(EOnlineServices::Null, Id.AccountIndex);
	
	StringToId.Add(UserId, Id);
	LocalUserMap.Add(PlatformUserId, Id);

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
		UE_LOG(LogOnlineServices, VeryVerbose, TEXT("[FOnlineAccountIdRegistryNull::ToReplicationData] StringToBytes on %s returned %d len"), *Id->Data, ReplicationData.Num())
		return ReplicationData;
	}

	return TArray<uint8>();
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
