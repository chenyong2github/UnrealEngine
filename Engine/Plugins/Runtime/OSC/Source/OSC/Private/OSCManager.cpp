// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCManager.h"

#include "IPAddress.h"
#include "OSCMessage.h"
#include "OSCMessagePacket.h"
#include "OSCBundle.h"
#include "OSCBundlePacket.h"
#include "OSCLog.h"
#include "SocketSubsystem.h"


namespace
{
	// Returns true if provided address was null and was able to
	// override with local host address, false if not.
	bool GetLocalHostAddress(FString& Address)
	{
		if (!Address.IsEmpty() && Address != TEXT("0"))
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
				Address = Addr->ToString(bAppendPort);
				return true;
			}
		}

		return false;
	}

	const FOSCType* GetOSCTypeAtIndex(const FOSCMessage& Message, const int32 Index)
	{
		const TSharedPtr<FOSCMessagePacket>& Packet = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
		if (Packet.IsValid())
		{
			TArray<FOSCType>& Args = Packet->GetArguments();
			if (Index >= Args.Num())
			{
				UE_LOG(LogOSC, Error, TEXT("Index '%d' out-of-bounds.  Message argument size = '%d'"), Index, Args.Num());
				return nullptr;
			}

			return &Args[Index];
		}

		return nullptr;
	}
} // namespace <>

UOSCServer* UOSCManager::CreateOSCServer(FString ReceiveIPAddress, int32 Port, bool bMulticastLoopback, bool bStartListening)
{
	if (GetLocalHostAddress(ReceiveIPAddress))
	{
		UE_LOG(LogOSC, Display, TEXT("OSCServer ReceiveAddress not specified. Using LocalHost IP: '%s'"), *ReceiveIPAddress);
	}

	UOSCServer* NewOSCServer = NewObject<UOSCServer>();
	NewOSCServer->Connect();
	NewOSCServer->SetMulticastLoopback(bMulticastLoopback);
	if (NewOSCServer->SetAddress(ReceiveIPAddress, Port))
	{
		if (bStartListening)
		{
			NewOSCServer->Listen();
		}
	}
	else
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to parse ReceiveAddress '%s' for OSCServer."), *ReceiveIPAddress);
	}

	return NewOSCServer;
}

UOSCClient* UOSCManager::CreateOSCClient(FString SendIPAddress, int32 Port)
{
	if (GetLocalHostAddress(SendIPAddress))
	{
		UE_LOG(LogOSC, Display, TEXT("OSCClient SendAddress not specified. Using LocalHost IP: '%s'"), *SendIPAddress);
	}

	UOSCClient* NewOSCClient = NewObject<UOSCClient>();
	NewOSCClient->Connect();
	if (!NewOSCClient->SetSendIPAddress(SendIPAddress, Port))
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to parse SendAddress '%s' for OSCClient. Client unable to send new messages."), *SendIPAddress);
	}

	return NewOSCClient;
}

FOSCMessage& UOSCManager::ClearMessage(FOSCMessage& Message)
{
	const TSharedPtr<FOSCMessagePacket>& Packet = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	if (Packet.IsValid())
	{
		Packet->GetArguments().Reset();
	}

	return Message;
}

FOSCBundle& UOSCManager::ClearBundle(FOSCBundle& Bundle)
{
	const TSharedPtr<FOSCBundlePacket>& Packet = StaticCastSharedPtr<FOSCBundlePacket>(Bundle.GetPacket());
	if (Packet.IsValid())
	{
		Packet->GetPackets().Reset();
	}

	return Bundle;
}

FOSCBundle& UOSCManager::AddMessageToBundle(const FOSCMessage& Message, FOSCBundle& Bundle)
{
	const TSharedPtr<FOSCBundlePacket>& BundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(Bundle.GetPacket());
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());

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

FOSCMessage& UOSCManager::AddFloat(FOSCMessage& Message, float Value)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddInt32(FOSCMessage& Message, int32 Value)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddInt64(FOSCMessage& Message, int64 Value)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddString(FOSCMessage& Message, FString Value)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddBlob(FOSCMessage& Message, TArray<uint8>& Value)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddBool(FOSCMessage& Message, bool Value)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	MessagePacket->GetArguments().Add(FOSCType(Value));
	return Message;
}

TArray<FOSCBundle>& UOSCManager::GetBundlesFromBundle(const FOSCBundle& Bundle, TArray<FOSCBundle>& Bundles)
{
	Bundles.Reset();
	if (Bundle.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCBundlePacket>& BundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(Bundle.GetPacket());
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

TArray<FOSCMessage>& UOSCManager::GetMessagesFromBundle(const FOSCBundle& Bundle, TArray<FOSCMessage>& Messages)
{
	Messages.Reset();
	if (Bundle.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCBundlePacket>& BundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(Bundle.GetPacket());
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

void UOSCManager::GetFloat(const FOSCMessage& Message, const int32 Index, float& Value)
{
	if (const FOSCType* OSCType = GetOSCTypeAtIndex(Message, Index))
	{
		if (OSCType->IsFloat())
		{
			Value = OSCType->GetFloat();
		}
	}
}

void UOSCManager::GetAllFloats(const FOSCMessage& Message, TArray<float>& Values)
{
	if (Message.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsFloat())
			{
				Values.Add(OSCType.GetFloat());
			}
		}
	}
}

void UOSCManager::GetInt32(const FOSCMessage& Message, const int32 Index, int32& Value)
{
	if (const FOSCType* OSCType = GetOSCTypeAtIndex(Message, Index))
	{
		if (OSCType->IsInt32())
		{
			Value = OSCType->GetInt32();
		}
	}
}

void UOSCManager::GetAllInt32s(const FOSCMessage& Message, TArray<int32>& Values)
{
	if (Message.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsInt32())
			{
				Values.Add(OSCType.GetInt32());
			}
		}
	}
}

void UOSCManager::GetInt64(const FOSCMessage& Message, const int32 Index, int64& Value)
{
	if (const FOSCType* OSCType = GetOSCTypeAtIndex(Message, Index))
	{
		if (OSCType->IsInt64())
		{
			Value = OSCType->GetInt64();
		}
	}
}

void UOSCManager::GetAllInt64s(const FOSCMessage& Message, TArray<int64>& Values)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	if (MessagePacket.IsValid())
	{
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsInt64())
			{
				Values.Add(OSCType.GetInt64());
			}
		}
	}
}

void UOSCManager::GetString(const FOSCMessage& Message, const int32 Index, FString& Value)
{
	if (const FOSCType* OSCType = GetOSCTypeAtIndex(Message, Index))
	{
		if (OSCType->IsString())
		{
			Value = OSCType->GetString();
		}
	}
}

void UOSCManager::GetAllStrings(const FOSCMessage& Message, TArray<FString>& Values)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	if (MessagePacket.IsValid())
	{
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsString())
			{
				Values.Add(OSCType.GetString());
			}
		}
	}
}

void UOSCManager::GetBool(const FOSCMessage& Message, const int32 Index, bool& Value)
{
	if (const FOSCType* OSCType = GetOSCTypeAtIndex(Message, Index))
	{
		if (OSCType->IsBool())
		{
			Value = OSCType->GetBool();
		}
	}
}

void UOSCManager::GetAllBools(const FOSCMessage& Message, TArray<bool>& Values)
{
	const TSharedPtr<FOSCMessagePacket>& MessagePacket = StaticCastSharedPtr<FOSCMessagePacket>(Message.GetPacket());
	if (MessagePacket.IsValid())
	{
		const TArray<FOSCType>& Args = MessagePacket->GetArguments();
		for (int32 i = 0; i < Args.Num(); i++)
		{
			const FOSCType& OSCType = Args[i];
			if (OSCType.IsBool())
			{
				Values.Add(OSCType.GetBool());
			}
		}
	}
}

void UOSCManager::GetBlob(const FOSCMessage& Message, const int32 Index, TArray<uint8>& Value)
{
	if (const FOSCType* OSCType = GetOSCTypeAtIndex(Message, Index))
	{
		if (OSCType->IsBlob())
		{
			Value = OSCType->GetBlob();
		}
	}
}

bool UOSCManager::OSCAddressIsValidPath(const FOSCAddress& Address)
{
	return Address.IsValidPath();
}

bool UOSCManager::OSCAddressIsValidPattern(const FOSCAddress& Address)
{
	return Address.IsValidPattern();
}

FOSCAddress UOSCManager::ConvertStringToOSCAddress(const FString& String)
{
	return FOSCAddress(String);
}

FOSCAddress& UOSCManager::OSCAddressPushContainer(FOSCAddress& Address, const FString& ToAppend)
{
	Address.PushContainer(ToAppend);
	return Address;
}

FOSCAddress& UOSCManager::OSCAddressPushContainers(FOSCAddress& Address, const TArray<FString>& ToAppend)
{
	Address.PushContainers(ToAppend);
	return Address;
}

FString UOSCManager::OSCAddressPopContainer(FOSCAddress& Address)
{
	return Address.PopContainer();
}

TArray<FString> UOSCManager::OSCAddressPopContainers(FOSCAddress& Address, int32 InNumContainers)
{
	return Address.PopContainers(InNumContainers);
}

bool UOSCManager::OSCAddressPathMatchesPattern(const FOSCAddress& InPattern, const FOSCAddress& InPath)
{
	return InPattern.Matches(InPath);
}

FOSCAddress UOSCManager::GetOSCMessageAddress(const FOSCMessage& Message)
{
	return Message.GetAddress();
}

FOSCMessage& UOSCManager::SetOSCMessageAddress(FOSCMessage& Message, const FOSCAddress& Address)
{
	Message.SetAddress(Address);
	return Message;
}

FString UOSCManager::GetOSCAddressContainer(const FOSCAddress& Address, int32 Index)
{
	return Address.GetContainer(Index);
}

TArray<FString> UOSCManager::GetOSCAddressContainers(const FOSCAddress& Address)
{
	TArray<FString> Containers;
	Address.GetContainers(Containers);
	return Containers;
}

FString UOSCManager::GetOSCAddressContainerPath(const FOSCAddress& Address)
{
	return Address.GetContainerPath();
}

FString UOSCManager::GetOSCAddressFullPath(const FOSCAddress& Address)
{
	return Address.GetFullPath();
}

FString UOSCManager::GetOSCAddressMethod(const FOSCAddress& Address)
{
	return Address.GetMethod();
}

FOSCAddress& UOSCManager::ClearOSCAddressContainers(FOSCAddress& Address)
{
	Address.ClearContainers();
	return Address;
}

FOSCAddress& UOSCManager::SetOSCAddressMethod(FOSCAddress& Address, const FString& Method)
{
	Address.SetMethod(Method);
	return Address;
}