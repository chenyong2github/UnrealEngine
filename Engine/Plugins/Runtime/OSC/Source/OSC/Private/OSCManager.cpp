// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCManager.h"

#include "IPAddress.h"
#include "OSCMessage.h"
#include "OSCBundle.h"
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
		const TSharedPtr<FOSCMessagePacket>& Packet = Message.GetPacket();
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
	UOSCServer* NewOSCServer = NewObject<UOSCServer>();

	if (GetLocalHostAddress(ReceiveIPAddress))
	{
		UE_LOG(LogOSC, Display, TEXT("OSCServer ReceiveAddress not specified. Using LocalHost IP: '%s'"), *ReceiveIPAddress);
	}

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
	UOSCClient* NewOSCClient = NewObject<UOSCClient>();

	if (GetLocalHostAddress(SendIPAddress))
	{
		UE_LOG(LogOSC, Display, TEXT("OSCClient SendAddress not specified. Using LocalHost IP: '%s'"), *SendIPAddress);
	}

	if (!NewOSCClient->SetSendIPAddress(SendIPAddress, Port))
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to parse SendAddress '%s' for OSCClient. Client unable to send new messages."), *SendIPAddress);
	}

	return NewOSCClient;
}

void UOSCManager::ClearMessage(FOSCMessage& Message)
{
	if (Message.GetPacket().IsValid())
	{
		Message.GetPacket()->GetArguments().Reset();
	}
}

void UOSCManager::ClearBundle(FOSCBundle& Bundle)
{
	if (Bundle.GetPacket().IsValid())
	{
		Bundle.GetPacket()->GetPackets().Reset();
	}
}

FOSCBundle& UOSCManager::AddMessageToBundle(const FOSCMessage& Message, FOSCBundle& Bundle)
{
	if (Message.GetPacket().IsValid() && Bundle.GetPacket().IsValid())
	{
		Bundle.GetPacket()->GetPackets().Add(Message.GetPacket());
	}

	return Bundle;
}

FOSCBundle& UOSCManager::AddBundleToBundle(const FOSCBundle& InBundle, FOSCBundle& OutBundle)
{
	if (InBundle.GetPacket().IsValid() && OutBundle.GetPacket().IsValid())
	{
		OutBundle.GetPacket()->GetPackets().Add(InBundle.GetPacket());
	}

	return OutBundle;
}

FOSCMessage& UOSCManager::AddFloat(FOSCMessage& Message, float Value)
{
	Message.GetPacket()->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddInt32(FOSCMessage& Message, int32 Value)
{
	Message.GetPacket()->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddInt64(FOSCMessage& Message, int64 Value)
{
	Message.GetPacket()->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddString(FOSCMessage& Message, FString Value)
{
	Message.GetPacket()->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddBlob(FOSCMessage& Message, TArray<uint8>& Value)
{
	Message.GetPacket()->GetArguments().Add(FOSCType(Value));
	return Message;
}

FOSCMessage& UOSCManager::AddBool(FOSCMessage& Message, bool Value)
{
	Message.GetPacket()->GetArguments().Add(FOSCType(Value));
	return Message;
}

void UOSCManager::GetMessagesFromBundle(const FOSCBundle& Bundle, TArray<FOSCMessage>& Messages)
{
	if (Bundle.GetPacket().IsValid())
	{
		const TSharedPtr<FOSCBundlePacket> BundlePacket = Bundle.GetPacket();
		for (int32 i = 0; i < BundlePacket->GetPackets().Num(); i++)
		{
			const TSharedPtr<FOSCPacket>& Packet = BundlePacket->GetPackets()[i];
			if (Packet->IsMessage())
			{
				Messages.Emplace(StaticCastSharedPtr<FOSCMessagePacket>(Packet));
			}
		}
	}
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
		const TArray<FOSCType>& Args = Message.GetPacket()->GetArguments();
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
		const TArray<FOSCType>& Args = Message.GetPacket()->GetArguments();
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
	if (Message.GetPacket().IsValid())
	{
		const TArray<FOSCType>& Args = Message.GetPacket()->GetArguments();
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
	if (Message.GetPacket().IsValid())
	{
		const TArray<FOSCType>& Args = Message.GetPacket()->GetArguments();
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
	if (Message.GetPacket().IsValid())
	{
		const TArray<FOSCType>& Args = Message.GetPacket()->GetArguments();
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

bool UOSCManager::OSCAddressIsBundle(const FOSCAddress& Address)
{
	return Address.IsBundle();
}

bool UOSCManager::OSCAddressIsMessage(const FOSCAddress& Address)
{
	return Address.IsBundle();
}

bool UOSCManager::OSCAddressIsValid(const FOSCAddress& Address)
{
	return Address.IsValid();
}

FOSCAddress UOSCManager::StringToOSCAddress(const FString& String)
{
	return FOSCAddress(String);
}

FOSCAddress& UOSCManager::OSCAddressAppend(FOSCAddress& Address, const FString& ToAppend)
{
	Address.Append(ToAppend);
	return Address;
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

TArray<FString> UOSCManager::SplitOSCAddress(const FOSCAddress& Address)
{
	TArray<FString> OutArray;
	Address.Split(OutArray);
	return MoveTemp(OutArray);
}