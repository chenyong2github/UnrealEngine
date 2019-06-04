// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCManager.h"
#include "OSCMessage.h"
#include "OSCBundle.h"
#include "OSCLog.h"

UOSCServer* UOSCManager::CreateOSCServer(FString ReceiveAddress, int32 Port)
{
	UOSCServer* NewOSCServer = NewObject<UOSCServer>();

	FIPv4Address ipAddr;
	if (FIPv4Address::Parse(ReceiveAddress, ipAddr))
	{
		NewOSCServer->Listen(ipAddr, Port, false);
	}

	return NewOSCServer;
}

UOSCClient* UOSCManager::CreateOSCClient(FString SendAddress, int32 Port)
{
	UOSCClient* NewOSCClient = NewObject<UOSCClient>();

	FIPv4Address ipAddr;
	if (FIPv4Address::Parse(SendAddress, ipAddr))
	{
		NewOSCClient->SetTarget(ipAddr, Port);
	}

	return NewOSCClient;
}

void UOSCManager::CreateOSCMessage(UPARAM(ref) FOSCMessage& Message)
{
	Message.SetPacket(MakeShareable(new FOSCMessagePacket()));
}

void UOSCManager::CreateOSCBundle(UPARAM(ref) FOSCBundle& Bundle)
{
	Bundle.SetPacket(MakeShareable(new FOSCBundlePacket()));
}

void UOSCManager::ClearMessage(UPARAM(ref) FOSCMessage& Message)
{
	if (Message.IsValid())
	{
		Message.GetPacket()->Clear();
	}
}

void UOSCManager::ClearBundle(UPARAM(ref) FOSCBundle& Bundle)
{
	if (Bundle.IsValid())
	{
		Bundle.GetPacket()->Clear();
	}
}

void UOSCManager::AddMessageToBundle(const FOSCMessage& Message, UPARAM(ref) FOSCBundle& Bundle)
{
	if (Message.IsValid() && Bundle.IsValid())
	{
		Bundle.GetPacket()->AddPacket(Message.GetPacket());
	}
}

void UOSCManager::AddBundleToBundle(const FOSCBundle& inBundle, UPARAM(ref) FOSCBundle& outBundle)
{
	if (inBundle.IsValid() && outBundle.IsValid())
	{
		outBundle.GetPacket()->AddPacket(inBundle.GetPacket());
	}
}

void UOSCManager::SetAddress(UPARAM(ref) FOSCMessage& Message, const FString& Address)
{
	Message.GetOrCreatePacket()->SetAddress(Address);
}

void UOSCManager::AddFloat(UPARAM(ref) FOSCMessage& Message, float Value)
{
	Message.GetOrCreatePacket()->AddArgument(FOSCType(Value));
}

void UOSCManager::AddInt32(UPARAM(ref) FOSCMessage& Message, int32 Value)
{
	Message.GetOrCreatePacket()->AddArgument(FOSCType(Value));
}

void UOSCManager::AddInt64(UPARAM(ref) FOSCMessage& Message, int64 Value)
{
	Message.GetOrCreatePacket()->AddArgument(FOSCType(Value));
}

void UOSCManager::AddString(UPARAM(ref) FOSCMessage& Message, FString Value)
{
	Message.GetOrCreatePacket()->AddArgument(FOSCType(Value));
}

void UOSCManager::AddBlob(UPARAM(ref) FOSCMessage& Message, TArray<uint8>& Value)
{
	Message.GetOrCreatePacket()->AddArgument(FOSCType(Value));
}

void UOSCManager::AddBool(UPARAM(ref) FOSCMessage& Message, bool Value)
{
	Message.GetOrCreatePacket()->AddArgument(FOSCType(Value));
}

void UOSCManager::GetMessagesFromBundle(const FOSCBundle& Bundle, TArray<FOSCMessage>& Messages)
{
	if (Bundle.IsValid())
	{
		const TSharedPtr<FOSCBundlePacket> bundlePacket = Bundle.GetPacket();

		const int32 num = bundlePacket->GetNumPackets();
		for (int32 i = 0; i < num; i++)
		{
			TSharedPtr<FOSCPacket> packet = bundlePacket->GetPacket(i);

			if (packet->IsMessage())
			{
				FOSCMessage message;

				message.SetPacket(StaticCastSharedPtr<FOSCMessagePacket>(packet));

				Messages.Add(message);
			}
		}
	}
}

void UOSCManager::GetFloat(const FOSCMessage& Message, const int index, float& Value)
{
	if (Message.IsValid())
	{
		const FOSCType& t = Message.GetPacket()->GetArgument(index);

		if (t.IsFloat())
		{
			Value = t.GetFloat();
		}
	}
}

void UOSCManager::GetAllFloat(const FOSCMessage& Message, TArray<float>& Values)
{
	if (Message.IsValid())
	{
		for (int i = 0; i < Message.GetPacket()->GetNumArguments(); i++)
		{
			const FOSCType& t = Message.GetPacket()->GetArgument(i);
			if (t.IsFloat())
			{
				Values.Add(t.GetFloat());
			}
		}
	}
}

void UOSCManager::GetInt32(const FOSCMessage& Message, const int index, int32& Value)
{
	if (Message.IsValid())
	{
		const FOSCType& t = Message.GetPacket()->GetArgument(index);

		if (t.IsInt32())
		{
			Value = t.GetInt32();
		}
	}
}

void UOSCManager::GetAllInt32(const FOSCMessage& Message, TArray<int32>& Values)
{
	if (Message.IsValid())
	{
		for (int i = 0; i < Message.GetPacket()->GetNumArguments(); i++)
		{
			const FOSCType& t = Message.GetPacket()->GetArgument(i);
			if (t.IsInt32())
			{
				Values.Add(t.GetInt32());
			}
		}
	}
}

void UOSCManager::GetInt64(const FOSCMessage& Message, const int index, int64& Value)
{
	if (Message.IsValid())
	{
		const FOSCType& t = Message.GetPacket()->GetArgument(index);

		if (t.IsInt64())
		{
			Value = t.GetInt64();
		}
	}
}

void UOSCManager::GetAllInt64(const FOSCMessage& Message, TArray<int64>& Values)
{
	if (Message.IsValid())
	{
		for (int i = 0; i < Message.GetPacket()->GetNumArguments(); i++)
		{
			const FOSCType& t = Message.GetPacket()->GetArgument(i);
			if (t.IsInt64())
			{
				Values.Add(t.GetInt64());
			}
		}
	}
}

void UOSCManager::GetString(const FOSCMessage& Message, const int index, FString& Value)
{
	if (Message.IsValid())
	{
		const FOSCType& t = Message.GetPacket()->GetArgument(index);

		if (t.IsString())
		{
			Value = t.GetString();
		}
	}
}

void UOSCManager::GetAllString(const FOSCMessage& Message, TArray<FString>& Values)
{
	if (Message.IsValid())
	{
		for (int i = 0; i < Message.GetPacket()->GetNumArguments(); i++)
		{
			const FOSCType& t = Message.GetPacket()->GetArgument(i);
			if (t.IsString())
			{
				Values.Add(t.GetString());
			}
		}
	}
}

void UOSCManager::GetBool(const FOSCMessage& Message, const int index, bool& Value)
{
	if (Message.IsValid())
	{
		const FOSCType& t = Message.GetPacket()->GetArgument(index);

		if (t.IsBool())
		{
			Value = t.GetBool();
		}
	}
}

void UOSCManager::GetAllBool(const FOSCMessage& Message, TArray<bool>& Values)
{
	if (Message.IsValid())
	{
		for (int i = 0; i < Message.GetPacket()->GetNumArguments(); i++)
		{
			const FOSCType& t = Message.GetPacket()->GetArgument(i);
			if (t.IsBool())
			{
				Values.Add(t.GetBool());
			}
		}
	}
}

void UOSCManager::GetBlob(const FOSCMessage& Message, const int index, TArray<uint8>& Value)
{
	if (Message.IsValid())
	{
		const FOSCType& t = Message.GetPacket()->GetArgument(index);

		if (t.IsBlob())
		{
			Value = t.GetBlob();
		}
	}
}
