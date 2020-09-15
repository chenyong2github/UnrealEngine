// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundRouter.h"
#include "MetasoundOperatorInterface.h"
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

// Convenience exec commands to push values to global params.
static FAutoConsoleCommand GPushFloatCommand(
	TEXT("au.metasound.SetFloat"),
	TEXT("Use this with au.metasound.SetFloat [address] [value]. Pushes a parameter value directly to a global address, which can then be received by Metasounds using a Receive node."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("au.metasound.SetFloat should be called with two args- the address to send to and the value to send."));
				return;
			}

			FName ChannelName = FName(*Args[0]);
			float ValueToPush = TCString<TCHAR>::Atof(*Args[1]);

			Metasound::FDataTypeLiteralParam LiteralParam(ValueToPush);

			if (!Metasound::FDataTransmissionCenter::Get().PushLiteral(ChannelName, LiteralParam))
			{
				UE_LOG(LogTemp, Warning, TEXT("au.metasound.SetFloat failed, likely because the channel did not support floats or the channel did not exist."));
			}
		})
);

static FAutoConsoleCommand GPushBoolCommand(
	TEXT("au.metasound.SetBool"),
	TEXT("Use this with au.metasound.SetBool [address] [value]. Pushes a parameter value directly to a global address, which can then be received by Metasounds using a Receive node."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("au.metasound.SetBool should be called with two args- the address to send to and the value to send."));
				return;
			}

			FName ChannelName = FName(*Args[0]);
			int32 ValueAsInt = TCString<TCHAR>::Atoi(*Args[1]);

			bool ValueToPush = ValueAsInt != 0;

			Metasound::FDataTypeLiteralParam LiteralParam(ValueToPush);

			if (!Metasound::FDataTransmissionCenter::Get().PushLiteral(ChannelName, LiteralParam))
			{
				UE_LOG(LogTemp, Warning, TEXT("au.metasound.SetBool failed, likely because the channel did not support floats or the channel did not exist."));
			}
		})
);

static FAutoConsoleCommand GPushIntCommand(
	TEXT("au.metasound.SetInt"),
	TEXT("Use this with au.metasound.SetInt [address] [value]. Pushes a parameter value directly to a global address, which can then be received by Metasounds using a Receive node."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("au.metasound.SetBool should be called with two args- the address to send to and the value to send."));
				return;
			}

			FName ChannelName = FName(*Args[0]);
			int32 ValueToPush = TCString<TCHAR>::Atoi(*Args[1]);

			Metasound::FDataTypeLiteralParam LiteralParam(ValueToPush);

			if (!Metasound::FDataTransmissionCenter::Get().PushLiteral(ChannelName, LiteralParam))
			{
				UE_LOG(LogTemp, Warning, TEXT("au.metasound.SetInt failed, likely because the channel did not support floats or the channel did not exist."));
			}
		})
);

static FAutoConsoleCommand GPushStringCommand(
	TEXT("au.metasound.SetString"),
	TEXT("Use this with au.metasound.SetString [address] [value]. Pushes a parameter value directly to a global address, which can then be received by Metasounds using a Receive node."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("au.metasound.SetBool should be called with two args- the address to send to and the value to send."));
				return;
			}

			FName ChannelName = FName(*Args[0]);

			Metasound::FDataTypeLiteralParam LiteralParam(Args[1]);

			if (!Metasound::FDataTransmissionCenter::Get().PushLiteral(ChannelName, LiteralParam))
			{
				UE_LOG(LogTemp, Warning, TEXT("au.metasound.SetString failed, likely because the channel did not support floats or the channel did not exist."));
			}
		})
);


namespace Metasound
{

	IReceiver::~IReceiver()
	{
		DataChannel->OnReceiverDestroyed();
	}

	ISender::~ISender()
	{
		DataChannel->OnSenderDestroyed();
	}
	
	ITransmissionSubsystem::ITransmissionSubsystem(FName InSubsystemName)
		: SubsystemName(InSubsystemName)
	{
		FDataTransmissionCenter::Get().RegisterSubsystem(this, SubsystemName);
	}

	ITransmissionSubsystem::~ITransmissionSubsystem()
	{
		FDataTransmissionCenter::Get().UnregisterSubsystem(SubsystemName);
	}

	Metasound::FDataTransmissionCenter& FDataTransmissionCenter::Get()
	{
		static FDataTransmissionCenter Singleton;
		return Singleton;
	}

	FDataTransmissionCenter::FDataTransmissionCenter()
	{
	}

	TArray<FName> FAddressRouter::GetAvailableChannels()
	{
		FScopeLock ScopeLock(&DataChannelMapMutationLock);

		TArray<FName> AvailableChannels;

		for (auto& DataChannelPair : DataChannelMap)
		{
			AvailableChannels.Add(DataChannelPair.Key);
		}

		return AvailableChannels;
	}

	FName FAddressRouter::GetDatatypeForChannel(FName InChannelName)
	{
		FScopeLock ScopeLock(&DataChannelMapMutationLock);

		if (TSharedRef<IDataChannel>* FoundChannel = DataChannelMap.Find(InChannelName))
		{
			return (*FoundChannel)->GetDataType();
		}
		else
		{
			return FName();
		}
	}

	TArray<FName> FInstanceLocalRouter::GetAvailableChannels(uint64 InInstanceID)
	{
		FScopeLock ScopeLock(&InstanceRouterMapMutationLock);

		if (FAddressRouter* AddressRouter = InstanceRouterMap.Find(InInstanceID))
		{
			return AddressRouter->GetAvailableChannels();
		}
		else
		{
			return TArray<FName>();
		}
	}

	FName FInstanceLocalRouter::GetDatatypeForChannel(uint64 InInstanceID, FName InChannelName)
	{
		FScopeLock ScopeLock(&InstanceRouterMapMutationLock);

		if (FAddressRouter* AddressRouter = InstanceRouterMap.Find(InInstanceID))
		{
			return AddressRouter->GetDatatypeForChannel(InChannelName);
		}
		else
		{
			return FName();
		}
	}

}
