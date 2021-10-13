// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundRouter.h"

#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundOperatorInterface.h"
#include "HAL/IConsoleManager.h"


// Convenience exec commands to push values to global params.
static FAutoConsoleCommand GPushFloatCommand(
	TEXT("au.MetaSound.SetFloat"),
	TEXT("Use this with au.MetaSound.SetFloat [type] [address] [value]. Pushes a parameter value directly to a global address, which can then be received by Metasounds using a Receive node."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
		{
			if (Args.Num() < 3)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("au.MetaSound.Set* should be called with three args- the data type of the channel, the address to send to and the value to send."));
				return;
			}

			FName DataType = FName(*Args[0]);
			FName ChannelName = FName(*Args[1]);
			float ValueToPush = TCString<TCHAR>::Atof(*Args[2]);

			Metasound::FLiteral LiteralParam(ValueToPush);

			Metasound::FDataTransmissionCenter::Get().PushLiteral(DataType, ChannelName, LiteralParam);
		})
);

static FAutoConsoleCommand GPushBoolCommand(
	TEXT("au.MetaSound.SetBool"),
	TEXT("Use this with au.MetaSound.SetBool [address] [value]. Pushes a parameter value directly to a global address, which can then be received by Metasounds using a Receive node."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("au.MetaSound.SetBool should be called with two args- the address to send to and the value to send."));
				UE_LOG(LogMetaSound, Warning, TEXT("au.MetaSound.Set* should be called with three args- the data type of the channel, the address to send to and the value to send."));
				return;
			}

			FName DataType = FName(*Args[0]);
			FName ChannelName = FName(*Args[1]);
			int32 ValueAsInt = TCString<TCHAR>::Atoi(*Args[2]);

			bool ValueToPush = ValueAsInt != 0;

			Metasound::FLiteral LiteralParam(ValueToPush);

			Metasound::FDataTransmissionCenter::Get().PushLiteral(DataType, ChannelName, LiteralParam);
		})
);

static FAutoConsoleCommand GPushIntCommand(
	TEXT("au.MetaSound.SetInt"),
	TEXT("Use this with au.MetaSound.SetInt [address] [value]. Pushes a parameter value directly to a global address, which can then be received by Metasounds using a Receive node."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("au.MetaSound.Set* should be called with three args- the data type of the channel, the address to send to and the value to send."));
				return;
			}

			FName DataType = FName(*Args[0]);
			FName ChannelName = FName(*Args[1]);
			int32 ValueToPush = TCString<TCHAR>::Atoi(*Args[2]);

			Metasound::FLiteral LiteralParam(ValueToPush);

			Metasound::FDataTransmissionCenter::Get().PushLiteral(DataType, ChannelName, LiteralParam);
		})
);

static FAutoConsoleCommand GPushStringCommand(
	TEXT("au.MetaSound.SetString"),
	TEXT("Use this with au.MetaSound.SetString [address] [value]. Pushes a parameter value directly to a global address, which can then be received by Metasounds using a Receive node."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
		{
			if (Args.Num() < 3)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("au.MetaSound.SetBool should be called with three args- the data type of the channel, the address to send to and the value to send."));
				UE_LOG(LogMetaSound, Warning, TEXT("au.MetaSound.Set* should be called with three args- the data type of the channel, the address to send to and the value to send."));
				return;
			}

			FName DataType = FName(*Args[0]);
			FName ChannelName = FName(*Args[1]);

			Metasound::FLiteral LiteralParam(Args[2]);

			Metasound::FDataTransmissionCenter::Get().PushLiteral(DataType, ChannelName, LiteralParam);
		})
);

namespace Metasound
{
	FSendAddress::FSendAddress(const FString& InChannelName)
	: ChannelName(*InChannelName)
	, DataType()
	, InstanceID(INDEX_NONE)
	{
	}

	FSendAddress::FSendAddress(const FName& InChannelName, const FName& InDataType, uint64 InInstanceID)
	: ChannelName(InChannelName)
	, DataType(InDataType)
	, InstanceID(InInstanceID)
	{
	}

	IReceiver::~IReceiver()
	{
		DataChannel->OnReceiverDestroyed();
	}

	ISender::~ISender()
	{
		DataChannel->OnSenderDestroyed();
	}

	FDataTransmissionCenter& FDataTransmissionCenter::Get()
	{
		static FDataTransmissionCenter Singleton;
		return Singleton;
	}

	TUniquePtr<ISender> FDataTransmissionCenter::RegisterNewSender(const FSendAddress& InAddress, const FSenderInitParams& InitParams)
	{
		return GlobalRouter.RegisterNewSender(InAddress, InitParams);
	}

	TUniquePtr<IReceiver> FDataTransmissionCenter::RegisterNewReceiver(const FSendAddress& InAddress, const FReceiverInitParams& InitParams)
	{
		return GlobalRouter.RegisterNewReceiver(InAddress, InitParams);
	}

	bool FDataTransmissionCenter::UnregisterDataChannel(const FSendAddress& InAddress)
	{
		return GlobalRouter.UnregisterDataChannel(InAddress);
	}

	bool FDataTransmissionCenter::UnregisterDataChannelIfUnconnected(const FSendAddress& InAddress)
	{
		return GlobalRouter.UnregisterDataChannelIfUnconnected(InAddress);
	}

	bool FDataTransmissionCenter::PushLiteral(FName DataType, FName GlobalChannelName, const FLiteral& InParam)
	{
		return GlobalRouter.PushLiteral(DataType, GlobalChannelName, InParam);
	}


	TSharedPtr<IDataChannel, ESPMode::ThreadSafe> FAddressRouter::FindDataChannel(const FSendAddress& InAddress)
	{
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> Channel;

		{
			FScopeLock ScopeLock(&DataChannelMapMutationLock);

			if (TSharedRef<IDataChannel, ESPMode::ThreadSafe>* ExistingChannelPtr = DataChannelMap.Find(InAddress))
			{
				Channel = *ExistingChannelPtr;
			}
		}

		return Channel;
	}

	TSharedPtr<IDataChannel, ESPMode::ThreadSafe> FAddressRouter::GetDataChannel(const FSendAddress& InAddress, const FOperatorSettings& InOperatorSettings)
	{
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> DataChannel = FindDataChannel(InAddress);

		if (!DataChannel.IsValid())
		{
			FScopeLock ScopeLock(&DataChannelMapMutationLock);

			// This is the first time we're seeing this, add it to the map.
			DataChannel = Metasound::Frontend::IDataTypeRegistry::Get().CreateDataChannel(InAddress.GetDataType(), InOperatorSettings);
			if (DataChannel.IsValid())
			{
				DataChannelMap.Add(InAddress, DataChannel.ToSharedRef());
			}
		}

		return DataChannel;
	}

	TUniquePtr<ISender> FAddressRouter::RegisterNewSender(const FSendAddress& InAddress, const FSenderInitParams& InitParams)
	{
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> DataChannel = GetDataChannel(InAddress, InitParams.OperatorSettings);

		if (DataChannel.IsValid())
		{
			return DataChannel->NewSender(InitParams);
		}
		else
		{
			return TUniquePtr<ISender>(nullptr);
		}
	}

	bool FAddressRouter::UnregisterDataChannel(const FSendAddress& InAddress)
	{
		FScopeLock ScopeLock(&DataChannelMapMutationLock);

		if (TSharedRef<IDataChannel, ESPMode::ThreadSafe>* Channel = DataChannelMap.Find(InAddress))
		{
			if (const int32 NumReceiversActive = Channel->Get().GetNumActiveReceivers())
			{
				UE_LOG(LogMetaSound, Verbose, TEXT("DataChannel '%s' shutting down with %d receivers active."), *InAddress.ToString(), NumReceiversActive);
			}

			if (const int32 NumSendersActive = Channel->Get().GetNumActiveSenders())
			{
				UE_LOG(LogMetaSound, Verbose, TEXT("DataChannel '%s' shutting down with %d senders active."), *InAddress.ToString(), NumSendersActive);
			}
		}

		return DataChannelMap.Remove(InAddress) > 0;
	}

	bool FAddressRouter::UnregisterDataChannelIfUnconnected(const FSendAddress& InAddress)
	{
		FScopeLock ScopeLock(&DataChannelMapMutationLock);

		if (TSharedRef<IDataChannel, ESPMode::ThreadSafe>* Channel = DataChannelMap.Find(InAddress))
		{
			if (0 == Channel->Get().GetNumActiveReceivers())
			{
				if (0 == Channel->Get().GetNumActiveSenders())
				{
					return DataChannelMap.Remove(InAddress) > 0;
				}
			}
		}

		return false;
	}

	TUniquePtr<IReceiver> FAddressRouter::RegisterNewReceiver(const FSendAddress& InAddress, const FReceiverInitParams& InitParams)
	{
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> DataChannel = GetDataChannel(InAddress, InitParams.OperatorSettings);

		if (DataChannel.IsValid())
		{
			return DataChannel->NewReceiver(InitParams);
		}
		else
		{
			return TUniquePtr<IReceiver>(nullptr);
		}
	}
}
