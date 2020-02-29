// Copyright Epic Games, Inc. All Rights Reserved.
#include "OSCManager.h"

#include "IPAddress.h"
#include "OSCMessage.h"
#include "OSCMessagePacket.h"
#include "OSCBundle.h"
#include "OSCBundlePacket.h"
#include "OSCLog.h"
#include "OSCServer.h"
#include "OSCClient.h"

#include "Engine/World.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "SocketSubsystem.h"
#include "UObject/UObjectIterator.h"


#define OSC_LOG_INVALID_TYPE_AT_INDEX(Type, Index, Msg) UE_LOG(LogOSC, Warning, TEXT("OSC Message Parse Failed: OSCType not %s: index '%i', OSCAddress '%s'"), TEXT(##Type), Index, *Msg.GetAddress().GetFullPath())

namespace OSC
{
	// Returns true if provided address was null and was able to
	// override with local host address, false if not.
	bool GetLocalHostAddress(FString& InAddress)
	{
		if (!InAddress.IsEmpty() && InAddress != TEXT("0"))
		{
			return false;
		}

		bool bCanBind = false;
		bool bAppendPort = false;
		if (ISocketSubsystem* SocketSys = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			const TSharedPtr<FInternetAddr> Addr = SocketSys->GetLocalHostAddr(*GLog, bCanBind);
			if (Addr.IsValid())
			{
				InAddress = Addr->ToString(bAppendPort);
				return true;
			}
		}

		return false;
	}

	const FOSCType* GetOSCTypeAtIndex(const FOSCMessage& InMessage, const int32 InIndex)
	{
		const TSharedPtr<FOSCMessagePacket>& Packet = StaticCastSharedPtr<FOSCMessagePacket>(InMessage.GetPacket());
		if (Packet.IsValid())
		{
			TArray<FOSCType>& Args = Packet->GetArguments();
			if (InIndex >= Args.Num())
			{
				UE_LOG(LogOSC, Warning, TEXT("Index '%d' out-of-bounds.  Message argument size = '%d'"), InIndex, Args.Num());
				return nullptr;
			}

			return &Args[InIndex];
		}

		return nullptr;
	}
} // namespace OSC


static FAutoConsoleCommand GOSCGetServerDiag(
	TEXT("osc.servers"),
	TEXT("Prints diagnostic information pertaining to the current OSC servers to the output log."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
		{
			FString LocalAddr;
			OSC::GetLocalHostAddress(LocalAddr);
			UE_LOG(LogOSC, Display, TEXT("Local IP: %s"), *LocalAddr);

			UE_LOG(LogOSC, Display, TEXT("OSC Servers:"));
			for (TObjectIterator<UOSCServer> Itr; Itr; ++Itr)
			{
				if (UOSCServer* Server = *Itr)
				{
					FString ToPrint = TEXT("    ") + Server->GetName();
					if (UWorld* World = Server->GetWorld())
					{
						ToPrint += TEXT("(") + World->GetName() + TEXT(")");
					}

					ToPrint += TEXT(", ");
					ToPrint += Server->GetIpAddress(true /* bIncludePort */);
					ToPrint += Server->IsActive() ? TEXT(" [Active]") : TEXT(" [Inactive]");

					UE_LOG(LogOSC, Display, TEXT("%s"), *ToPrint);

					const TArray<FOSCAddress> BoundPatterns = Server->GetBoundOSCAddressPatterns();
					if (BoundPatterns.Num() > 0)
					{
						UE_LOG(LogOSC, Display, TEXT("\n    Bound Address Patterns:"));
						for (const FOSCAddress& Pattern : BoundPatterns)
						{
							UE_LOG(LogOSC, Display, TEXT("\n         %s"), *Pattern.GetFullPath());
						}
						UE_LOG(LogOSC, Display, TEXT(""));
					}
				}
			}
		}
	)
);

static FAutoConsoleCommand GOSCGetClientDiag(
	TEXT("osc.clients"),
	TEXT("Prints diagnostic information pertaining to the current OSC clients to the output log."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
		{
			FString LocalAddr;
			OSC::GetLocalHostAddress(LocalAddr);
			UE_LOG(LogOSC, Display, TEXT("Local IP: %s"), *LocalAddr);

			UE_LOG(LogOSC, Display, TEXT("OSC Clients:"));
			for (TObjectIterator<UOSCClient> Itr; Itr; ++Itr)
			{
				if (UOSCClient* Client = *Itr)
				{
					FString ToPrint;
					ToPrint += TEXT("\n    ") + Client->GetName();

					if (UWorld* World = Client->GetWorld())
					{
						ToPrint += TEXT("(") + World->GetName() + TEXT(")");
					}

					FString IPAddrStr;
					int32 Port;
					Client->GetSendIPAddress(IPAddrStr, Port);
					ToPrint += TEXT(", ") + IPAddrStr + TEXT(":");
					ToPrint.AppendInt(Port);
					ToPrint += Client->IsActive() ? TEXT(" [Active]") : TEXT(" [Inactive]");
					UE_LOG(LogOSC, Display, TEXT("%s"), *ToPrint);
				}
			}
		}
	)
);


UOSCServer* UOSCManager::CreateOSCServer(FString InReceiveIPAddress, int32 InPort, bool bInMulticastLoopback, bool bInStartListening)
{
	if (OSC::GetLocalHostAddress(InReceiveIPAddress))
	{
		UE_LOG(LogOSC, Display, TEXT("OSCServer ReceiveAddress not specified. Using LocalHost IP: '%s'"), *InReceiveIPAddress);
	}

	UOSCServer* NewOSCServer = NewObject<UOSCServer>();
	NewOSCServer->Connect();
	NewOSCServer->SetMulticastLoopback(bInMulticastLoopback);
	if (NewOSCServer->SetAddress(InReceiveIPAddress, InPort))
	{
		if (bInStartListening)
		{
			NewOSCServer->Listen();
		}
	}
	else
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to parse ReceiveAddress '%s' for OSCServer."), *InReceiveIPAddress);
	}

	return NewOSCServer;
}

UOSCClient* UOSCManager::CreateOSCClient(FString InSendIPAddress, int32 InPort)
{
	if (OSC::GetLocalHostAddress(InSendIPAddress))
	{
		UE_LOG(LogOSC, Display, TEXT("OSCClient SendAddress not specified. Using LocalHost IP: '%s'"), *InSendIPAddress);
	}

	UOSCClient* NewOSCClient = NewObject<UOSCClient>();
	NewOSCClient->Connect();
	if (!NewOSCClient->SetSendIPAddress(InSendIPAddress, InPort))
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to parse SendAddress '%s' for OSCClient. Client unable to send new messages."), *InSendIPAddress);
	}

	return NewOSCClient;
}

FOSCMessage& UOSCManager::ClearMessage(FOSCMessage& OutMessage)
{
	const TSharedPtr<FOSCMessagePacket>& Packet = StaticCastSharedPtr<FOSCMessagePacket>(OutMessage.GetPacket());
	if (Packet.IsValid())
	{
		Packet->GetArguments().Reset();
	}

	return OutMessage;
}

FOSCBundle& UOSCManager::ClearBundle(FOSCBundle& OutBundle)
{
	const TSharedPtr<FOSCBundlePacket>& Packet = StaticCastSharedPtr<FOSCBundlePacket>(OutBundle.GetPacket());
	if (Packet.IsValid())
	{
		Packet->GetPackets().Reset();
	}

	return OutBundle;
}

FOSCBundle& UOSCManager::AddMessageToBundle(const FOSCMessage& InMessage, FOSCBundle& Bundle)
{
	const TSharedPtr<FOSCBundlePacket>& BundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(Bundle.GetPacket());
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(InMessage.GetPacket());

	if (MessagePacket.IsValid() && BundlePacket.IsValid())
	{
		BundlePacket->GetPackets().Add(MessagePacket);
	}

	return Bundle;
}

FOSCBundle& UOSCManager::AddBundleToBundle(const FOSCBundle& InBundle, FOSCBundle& OutBundle)
{
	const TSharedPtr<FOSCBundlePacket>& InBundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(InBundle.GetPacket());
	const TSharedPtr<FOSCBundlePacket>& OutBundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(OutBundle.GetPacket());

	if (InBundlePacket.IsValid() && OutBundlePacket.IsValid())
	{
		InBundlePacket->GetPackets().Add(OutBundlePacket);
	}

	return OutBundle;
}

FOSCMessage& UOSCManager::AddFloat(FOSCMessage& OutMessage, float InValue)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(OutMessage.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(InValue));
	return OutMessage;
}

FOSCMessage& UOSCManager::AddInt32(FOSCMessage& OutMessage, int32 InValue)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(OutMessage.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(InValue));
	return OutMessage;
}

FOSCMessage& UOSCManager::AddInt64(FOSCMessage& OutMessage, int64 InValue)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(OutMessage.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(InValue));
	return OutMessage;
}

FOSCMessage& UOSCManager::AddAddress(FOSCMessage& OutMessage, const FOSCAddress& InValue)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(OutMessage.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(InValue.GetFullPath()));
	return OutMessage;
}

FOSCMessage& UOSCManager::AddString(FOSCMessage& OutMessage, FString InValue)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(OutMessage.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(InValue));
	return OutMessage;
}

FOSCMessage& UOSCManager::AddBlob(FOSCMessage& OutMessage, TArray<uint8>& OutValue)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(OutMessage.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(OutValue));
	return OutMessage;
}

FOSCMessage& UOSCManager::AddBool(FOSCMessage& OutMessage, bool InValue)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(OutMessage.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(InValue));
	return OutMessage;
}

TArray<FOSCBundle> UOSCManager::GetBundlesFromBundle(const FOSCBundle& InBundle)
{
	TArray<FOSCBundle> Bundles;
	if (InBundle.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCBundlePacket>& BundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(InBundle.GetPacket());
		for (int32 i = 0; i < BundlePacket->GetPackets().Num(); i++)
		{
			const TSharedPtr<IOSCPacket>& Packet = BundlePacket->GetPackets()[i];
			if (Packet->IsBundle())
			{
				Bundles.Emplace(StaticCastSharedPtr<FOSCMessagePacket>(Packet));
			}
		}
	}

	return Bundles;
}

FOSCMessage UOSCManager::GetMessageFromBundle(const FOSCBundle& InBundle, int32 InIndex, bool& bOutSucceeded)
{
	if (InBundle.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCBundlePacket>& BundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(InBundle.GetPacket());
		int32 Count = 0;
		for (const TSharedPtr<IOSCPacket>& Packet : BundlePacket->GetPackets())
		{
			if (Packet->IsMessage())
			{
				if (InIndex == Count)
				{
					bOutSucceeded = true;
					return FOSCMessage(StaticCastSharedPtr<FOSCMessagePacket>(Packet));
				}
				Count++;
			}
		}
	}

	bOutSucceeded = false;
	return FOSCMessage();
}

TArray<FOSCMessage> UOSCManager::GetMessagesFromBundle(const FOSCBundle& OutBundle)
{
	TArray<FOSCMessage> Messages;
	if (OutBundle.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCBundlePacket>& BundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(OutBundle.GetPacket());
		for (int32 i = 0; i < BundlePacket->GetPackets().Num(); i++)
		{
			const TSharedPtr<IOSCPacket>& Packet = BundlePacket->GetPackets()[i];
			if (Packet->IsMessage())
			{
				Messages.Emplace(StaticCastSharedPtr<FOSCMessagePacket>(Packet));
			}
		}
	}
	
	return Messages;
}

bool UOSCManager::GetAddress(const FOSCMessage& InMessage, const int32 InIndex, FOSCAddress& OutValue)
{
	if (const FOSCType* OSCType = OSC::GetOSCTypeAtIndex(InMessage, InIndex))
	{
		if (OSCType->IsString())
		{
			OutValue = FOSCAddress(OSCType->GetString());
			return OutValue.IsValidPath();
		}
		OSC_LOG_INVALID_TYPE_AT_INDEX("String (OSCAddress)", InIndex, InMessage);
	}

	OutValue = FOSCAddress();
	return false;
}

void UOSCManager::GetAllAddresses(const FOSCMessage& InMessage, TArray<FOSCAddress>& OutValues)
{
	if (InMessage.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(InMessage.GetPacket());
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsString())
			{
				FOSCAddress AddressToAdd = FOSCAddress(OSCType.GetString());
				if (AddressToAdd.IsValidPath())
				{
					OutValues.Add(MoveTemp(AddressToAdd));
				}
			}
		}
	}
}

bool UOSCManager::GetFloat(const FOSCMessage& InMessage, const int32 InIndex, float& OutValue)
{
	OutValue = 0.0f;
	if (const FOSCType* OSCType = OSC::GetOSCTypeAtIndex(InMessage, InIndex))
	{
		if (OSCType->IsFloat())
		{
			OutValue = OSCType->GetFloat();
			return true;
		}
		OSC_LOG_INVALID_TYPE_AT_INDEX("Float", InIndex, InMessage);
	}

	return false;
}

void UOSCManager::GetAllFloats(const FOSCMessage& InMessage, TArray<float>& OutValues)
{
	if (InMessage.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(InMessage.GetPacket());
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsFloat())
			{
				OutValues.Add(OSCType.GetFloat());
			}
		}
	}
}

bool UOSCManager::GetInt32(const FOSCMessage& InMessage, const int32 InIndex, int32& OutValue)
{
	OutValue = 0;
	if (const FOSCType* OSCType = OSC::GetOSCTypeAtIndex(InMessage, InIndex))
	{
		if (OSCType->IsInt32())
		{
			OutValue = OSCType->GetInt32();
			return true;
		}
		OSC_LOG_INVALID_TYPE_AT_INDEX("Int32", InIndex, InMessage);
	}

	return false;
}

void UOSCManager::GetAllInt32s(const FOSCMessage& InMessage, TArray<int32>& OutValues)
{
	if (InMessage.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(InMessage.GetPacket());
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsInt32())
			{
				OutValues.Add(OSCType.GetInt32());
			}
		}
	}
}

bool UOSCManager::GetInt64(const FOSCMessage& InMessage, const int32 InIndex, int64& OutValue)
{
	OutValue = 0l;
	if (const FOSCType* OSCType = OSC::GetOSCTypeAtIndex(InMessage, InIndex))
	{
		if (OSCType->IsInt64())
		{
			OutValue = OSCType->GetInt64();
			return true;
		}
		OSC_LOG_INVALID_TYPE_AT_INDEX("Int64", InIndex, InMessage);
	}

	return false;
}

void UOSCManager::GetAllInt64s(const FOSCMessage& InMessage, TArray<int64>& OutValues)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(InMessage.GetPacket());
	if (MessagePacket.IsValid())
	{
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsInt64())
			{
				OutValues.Add(OSCType.GetInt64());
			}
		}
	}
}

bool UOSCManager::GetString(const FOSCMessage& InMessage, const int32 InIndex, FString& OutValue)
{
	if (const FOSCType* OSCType = OSC::GetOSCTypeAtIndex(InMessage, InIndex))
	{
		if (OSCType->IsString())
		{
			OutValue = OSCType->GetString();
			return true;
		}
		OSC_LOG_INVALID_TYPE_AT_INDEX("String", InIndex, InMessage);
	}

	OutValue.Reset();
	return false;
}

void UOSCManager::GetAllStrings(const FOSCMessage& InMessage, TArray<FString>& OutValues)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(InMessage.GetPacket());
	if (MessagePacket.IsValid())
	{
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsString())
			{
				OutValues.Add(OSCType.GetString());
			}
		}
	}
}

bool UOSCManager::GetBool(const FOSCMessage& InMessage, const int32 InIndex, bool& OutValue)
{
	OutValue = false;
	if (const FOSCType* OSCType = OSC::GetOSCTypeAtIndex(InMessage, InIndex))
	{
		if (OSCType->IsBool())
		{
			OutValue = OSCType->GetBool();
			return true;
		}
		OSC_LOG_INVALID_TYPE_AT_INDEX("Bool", InIndex, InMessage);
	}

	return false;
}

void UOSCManager::GetAllBools(const FOSCMessage& InMessage, TArray<bool>& OutValues)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(InMessage.GetPacket());
	if (MessagePacket.IsValid())
	{
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsBool())
			{
				OutValues.Add(OSCType.GetBool());
			}
		}
	}
}

bool UOSCManager::GetBlob(const FOSCMessage& InMessage, const int32 InIndex, TArray<uint8>& OutValue)
{
	OutValue.Reset();
	if (const FOSCType* OSCType = OSC::GetOSCTypeAtIndex(InMessage, InIndex))
	{
		if (OSCType->IsBlob())
		{
			OutValue = OSCType->GetBlob();
			return true;
		}
		OSC_LOG_INVALID_TYPE_AT_INDEX("Blob", InIndex, InMessage);
	}

	return false;
}

bool UOSCManager::OSCAddressIsValidPath(const FOSCAddress& InAddress)
{
	return InAddress.IsValidPath();
}

bool UOSCManager::OSCAddressIsValidPattern(const FOSCAddress& InAddress)
{
	return InAddress.IsValidPattern();
}

FOSCAddress UOSCManager::ConvertStringToOSCAddress(const FString& InString)
{
	return FOSCAddress(InString);
}

UObject* UOSCManager::FindObjectAtOSCAddress(const FOSCAddress& InAddress)
{
	FSoftObjectPath Path(ObjectPathFromOSCAddress(InAddress));
	if (Path.IsValid())
	{
		return Path.TryLoad();
	}

	UE_LOG(LogOSC, Verbose, TEXT("Failed to load object from OSCAddress '%s'"), *InAddress.GetFullPath());
	return nullptr;
}

FOSCAddress UOSCManager::OSCAddressFromObjectPath(UObject* InObject)
{
	const FString Path = FPaths::ChangeExtension(InObject->GetPathName(), FString());
	return FOSCAddress(Path);
}

FOSCAddress UOSCManager::OSCAddressFromObjectPathString(const FString& InPathName)
{
	TArray<FString> PartArray;
	InPathName.ParseIntoArray(PartArray, TEXT("\'"));

	// Type declaration at beginning of path. Assumed to be in the form <SomeTypeContainer1'/Container2/ObjectName.ObjectName'>
	if (PartArray.Num() > 1)
	{
		const FString NoExtPath = FPaths::SetExtension(PartArray[1], TEXT(""));
		return FOSCAddress(NoExtPath);
	}

	// No type declaration at beginning of path. Assumed to be in the form <Container1/Container2/ObjectName.ObjectName>
	if (PartArray.Num() > 0)
	{
		const FString NoExtPath = FPaths::SetExtension(PartArray[0], TEXT(""));
		return FOSCAddress(NoExtPath);
	}

	// Invalid address
	return FOSCAddress();
}

FString UOSCManager::ObjectPathFromOSCAddress(const FOSCAddress& InAddress)
{
	const FString Path = InAddress.GetFullPath() + TEXT(".") + InAddress.GetMethod();
	return Path;
}

FOSCAddress& UOSCManager::OSCAddressPushContainer(FOSCAddress& OutAddress, const FString& InToAppend)
{
	OutAddress.PushContainer(InToAppend);
	return OutAddress;
}

FOSCAddress& UOSCManager::OSCAddressPushContainers(FOSCAddress& OutAddress, const TArray<FString>& InToAppend)
{
	OutAddress.PushContainers(InToAppend);
	return OutAddress;
}

FString UOSCManager::OSCAddressPopContainer(FOSCAddress& OutAddress)
{
	return OutAddress.PopContainer();
}

TArray<FString> UOSCManager::OSCAddressPopContainers(FOSCAddress& OutAddress, int32 InNumContainers)
{
	return OutAddress.PopContainers(InNumContainers);
}

FOSCAddress& UOSCManager::OSCAddressRemoveContainers(FOSCAddress& OutAddress, int32 InIndex, int32 InCount)
{
	OutAddress.RemoveContainers(InIndex, InCount);
	return OutAddress;
}

bool UOSCManager::OSCAddressPathMatchesPattern(const FOSCAddress& InPattern, const FOSCAddress& InPath)
{
	return InPattern.Matches(InPath);
}

FOSCAddress UOSCManager::GetOSCMessageAddress(const FOSCMessage& InMessage)
{
	return InMessage.GetAddress();
}

FOSCMessage& UOSCManager::SetOSCMessageAddress(FOSCMessage& OutMessage, const FOSCAddress& InAddress)
{
	OutMessage.SetAddress(InAddress);
	return OutMessage;
}

FString UOSCManager::GetOSCAddressContainer(const FOSCAddress& InAddress, int32 InIndex)
{
	return InAddress.GetContainer(InIndex);
}

TArray<FString> UOSCManager::GetOSCAddressContainers(const FOSCAddress& InAddress)
{
	TArray<FString> Containers;
	InAddress.GetContainers(Containers);
	return Containers;
}

FString UOSCManager::GetOSCAddressContainerPath(const FOSCAddress& InAddress)
{
	return InAddress.GetContainerPath();
}

FString UOSCManager::GetOSCAddressFullPath(const FOSCAddress& InAddress)
{
	return InAddress.GetFullPath();
}

FString UOSCManager::GetOSCAddressMethod(const FOSCAddress& InAddress)
{
	return InAddress.GetMethod();
}

FOSCAddress& UOSCManager::ClearOSCAddressContainers(FOSCAddress& OutAddress)
{
	OutAddress.ClearContainers();
	return OutAddress;
}

FOSCAddress& UOSCManager::SetOSCAddressMethod(FOSCAddress& OutAddress, const FString& InMethod)
{
	OutAddress.SetMethod(InMethod);
	return OutAddress;
}