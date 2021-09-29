// Copyright Epic Games, Inc. All Rights Reserved.

#include "SocketSubsystemEOS.h"
#include "InternetAddrEOS.h"
#include "SocketEOS.h"
#include "SocketTypes.h"
#include "Containers/Ticker.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSessionSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSubsystemEOS.h"
#include "UserManagerEOS.h"
#include "SocketSubsystemModule.h"

FSocketSubsystemEOS::FSocketSubsystemEOS(FOnlineSubsystemEOS* InSubsystemEOS)
	: SubsystemEOS(InSubsystemEOS)
	, LastSocketError(ESocketErrors::SE_NO_ERROR)
{
}

FSocketSubsystemEOS::~FSocketSubsystemEOS() = default;

bool FSocketSubsystemEOS::Init(FString& Error)
{
	FSocketSubsystemModule& SocketSubsystem = FModuleManager::LoadModuleChecked<FSocketSubsystemModule>("Sockets");
	SocketSubsystem.RegisterSocketSubsystem(EOS_SUBSYSTEM, this, false);

	return true;
}

void FSocketSubsystemEOS::Shutdown()
{
	// Destruct our sockets before we finish destructing, as they maintain a reference to us
	TrackedSockets.Reset();

	if (FSocketSubsystemModule* SocketSubsystem = FModuleManager::GetModulePtr<FSocketSubsystemModule>("Sockets"))
	{
		SocketSubsystem->UnregisterSocketSubsystem(EOS_SUBSYSTEM);
	}
}

FSocket* FSocketSubsystemEOS::CreateSocket(const FName& SocketTypeName, const FString& SocketDescription, const FName& /*unused*/)
{
	return TrackedSockets.Emplace_GetRef(MakeUnique<FSocketEOS>(*this, SocketDescription)).Get();
}

FResolveInfoCached* FSocketSubsystemEOS::CreateResolveInfoCached(TSharedPtr<FInternetAddr> Addr) const
{
	return nullptr;
}

void FSocketSubsystemEOS::DestroySocket(FSocket* Socket)
{
	for (auto It = TrackedSockets.CreateIterator(); It; ++It)
	{
		if (It->IsValid() && It->Get() == Socket)
		{
			It.RemoveCurrent();
			return;
		}
	}
}

FAddressInfoResult FSocketSubsystemEOS::GetAddressInfo(const TCHAR* HostName, const TCHAR* ServiceName, EAddressInfoFlags /*unused*/, const FName /*unused*/, ESocketType /*unused*/)
{
	return FAddressInfoResult(HostName, ServiceName);
}

bool FSocketSubsystemEOS::RequiresChatDataBeSeparate()
{
	return false;
}

bool FSocketSubsystemEOS::RequiresEncryptedPackets()
{
	return false;
}

bool FSocketSubsystemEOS::GetHostName(FString& HostName)
{
	return false;
}

TSharedRef<FInternetAddr> FSocketSubsystemEOS::CreateInternetAddr()
{
	return MakeShared<FInternetAddrEOS>();
}

TSharedPtr<FInternetAddr> FSocketSubsystemEOS::GetAddressFromString(const FString& InString)
{
	bool bUnused;
	TSharedPtr<FInternetAddrEOS> NewAddress = StaticCastSharedRef<FInternetAddrEOS>(CreateInternetAddr());
	NewAddress->SetIp(*InString, bUnused);
	return NewAddress;
}

bool FSocketSubsystemEOS::HasNetworkDevice()
{
	return true;
}

const TCHAR* FSocketSubsystemEOS::GetSocketAPIName() const
{
	return TEXT("p2pSocketsEOS");
}

ESocketErrors FSocketSubsystemEOS::GetLastErrorCode()
{
	return TranslateErrorCode(LastSocketError);
}

ESocketErrors FSocketSubsystemEOS::TranslateErrorCode(int32 Code)
{
	return static_cast<ESocketErrors>(Code);
}

bool FSocketSubsystemEOS::GetLocalAdapterAddresses(TArray<TSharedPtr<FInternetAddr>>& OutAddresses)
{
	TSharedRef<FInternetAddr> AdapterAddress = GetLocalBindAddr(nullptr, *GLog);
	OutAddresses.Add(AdapterAddress);
	return true;
}

TArray<TSharedRef<FInternetAddr>> FSocketSubsystemEOS::GetLocalBindAddresses()
{
	TArray<TSharedRef<FInternetAddr>> OutAddresses;
	OutAddresses.Add(GetLocalBindAddr(nullptr, *GLog));
	return OutAddresses;
}

TSharedRef<FInternetAddr> FSocketSubsystemEOS::GetLocalBindAddr(FOutputDevice& Out)
{
	return GetLocalBindAddr(nullptr, Out);
}

#if WITH_EOS_SDK
EOS_HP2P FSocketSubsystemEOS::GetP2PHandle()
{
	check(SubsystemEOS != nullptr);
	return SubsystemEOS->P2PHandle;
}

EOS_ProductUserId FSocketSubsystemEOS::GetLocalUserId()
{
	if (SubsystemEOS != nullptr)
	{
		return SubsystemEOS->UserManager->GetLocalProductUserId();
	}
	return nullptr;
}
#endif

TSharedRef<FInternetAddr> FSocketSubsystemEOS::GetLocalBindAddr(const UWorld* const OwningWorld, FOutputDevice& Out)
{
	TSharedRef<FInternetAddrEOS> BoundAddr = MakeShared<FInternetAddrEOS>();

#if WITH_EOS_SDK
	EOS_ProductUserId LocalUserId = GetLocalUserId();
	if (LocalUserId == nullptr)
	{
		UE_LOG(LogSocketSubsystemEOS, Error, TEXT("No local user to send p2p packets with"));
		return BoundAddr;
	}
	BoundAddr->SetLocalUserId(LocalUserId);
#else
	return BoundAddr;
#endif

	FString SessionId;
	// Find our current session id from the Default online subsystem
	if (const IOnlineSubsystem* const DefaultSubsystem = Online::GetSubsystem(OwningWorld))
	{
		const IOnlineSessionPtr DefaultSessionInt = DefaultSubsystem->GetSessionInterface();
		if (DefaultSessionInt.IsValid())
		{
			if (const FNamedOnlineSession* const NamedSession = DefaultSessionInt->GetNamedSession(NAME_GameSession))
			{
				SessionId = NamedSession->GetSessionIdStr();
			}
		}
	}

	if (SessionId.IsEmpty())
	{
		SessionId = FName(NAME_GameSession).ToString();
	}

	BoundAddr->SetSocketName(SessionId);

	return BoundAddr;
}

bool FSocketSubsystemEOS::IsSocketWaitSupported() const
{
	return false;
}

void FSocketSubsystemEOS::SetLastSocketError(const ESocketErrors NewSocketError)
{
	LastSocketError = NewSocketError;
}

bool FSocketSubsystemEOS::BindChannel(const FInternetAddrEOS& Address)
{
	if (!Address.IsValid())
	{
		SetLastSocketError(ESocketErrors::SE_EINVAL);
		return false;
	}

	const uint8 Channel = Address.GetChannel();

	FChannelSet& ExistingBoundPorts = BoundAddresses.FindOrAdd(Address.GetSocketName());
	if (ExistingBoundPorts.Contains(Channel))
	{
		SetLastSocketError(ESocketErrors::SE_EADDRINUSE);
		return false;
	}

	ExistingBoundPorts.Add(Channel);
	return true;
}

bool FSocketSubsystemEOS::UnbindChannel(const FInternetAddrEOS& Address)
{
	if (!Address.IsValid())
	{
		SetLastSocketError(ESocketErrors::SE_EINVAL);
		return false;
	}

	const FString SocketName = Address.GetSocketName();
	const uint8 Channel = Address.GetChannel();

	// Find our sessions collection of ports
	FChannelSet* ExistingBoundPorts = BoundAddresses.Find(SocketName);
	if (!ExistingBoundPorts)
	{
		SetLastSocketError(ESocketErrors::SE_ENOTSOCK);
		return false;
	}

	// Remove our port and check if we had it bound
	if (ExistingBoundPorts->Remove(Channel) == 0)
	{
		SetLastSocketError(ESocketErrors::SE_ENOTSOCK);
		return false;
	}

	// Remove any empty sets
	if (ExistingBoundPorts->Num() == 0)
	{
		BoundAddresses.Remove(SocketName);
		ExistingBoundPorts = nullptr;
	}

	return true;
}
