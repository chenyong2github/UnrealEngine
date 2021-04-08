// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundRouter.h"

#include "MetasoundFrontendRegistries.h"
#include "MetasoundOperatorInterface.h"
#include "CoreMinimal.h"
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

	FDataTransmissionCenter& FDataTransmissionCenter::Get()
	{
		static FDataTransmissionCenter Singleton;
		return Singleton;
	}

	TUniquePtr<ISender> FDataTransmissionCenter::RegisterNewSender(const FName& InDataTypeName, const FSendAddress& InAddress, const FSenderInitParams& InitParams)
	{
		TUniquePtr<ISender> Sender;

		if (InAddress.Subsystem == GetSubsystemNameForSendScope(ETransmissionScope::ThisInstanceOnly))
		{
			Sender = InstanceRouter.RegisterNewSender(InAddress.MetasoundInstanceID, InDataTypeName, InAddress.ChannelName, InitParams);
		}
		else if (InAddress.Subsystem == GetSubsystemNameForSendScope(ETransmissionScope::Global))
		{
			Sender = GlobalRouter.RegisterNewSender(InDataTypeName, InAddress.ChannelName, InitParams);
		}
		else if (FSubsystemData* FoundSubsystem = SubsystemRouters.Find(InAddress.Subsystem))
		{
			ITransmissionSubsystem* SubsystemInterface = FoundSubsystem->SubsystemPtr;

			// Ensure that this subsystem can support this send type.
			if (ensureAlways(SubsystemInterface && SubsystemInterface->CanSupportDataType(InDataTypeName)))
			{

				FScopeLock ScopeLock(&SubsystemRoutersMutationLock);
				Sender = FoundSubsystem->AddressRouter.RegisterNewSender(InDataTypeName, InAddress.ChannelName, InitParams);

				FoundSubsystem->SubsystemPtr->OnNewSendRegistered(InAddress, InDataTypeName);
			}
		}
		else
		{
			// Otherwise, the subsystem FName was invalid.
			UE_LOG(LogMetaSound, Error, TEXT("Cannot create Sender. Did not find transmission subsystem [Name:%s]"), *InAddress.Subsystem.ToString());
		}

		return MoveTemp(Sender);
	}

	TUniquePtr<IReceiver> FDataTransmissionCenter::RegisterNewReceiver(const FName& InDataTypeName, const FSendAddress& InAddress, const FReceiverInitParams& InitParams)
	{
		TUniquePtr<IReceiver> Receiver;

		if (InAddress.Subsystem == GetSubsystemNameForSendScope(ETransmissionScope::ThisInstanceOnly))
		{
			Receiver = InstanceRouter.RegisterNewReceiver(InAddress.MetasoundInstanceID, InDataTypeName, InAddress.ChannelName, InitParams);
		}
		else if (InAddress.Subsystem == GetSubsystemNameForSendScope(ETransmissionScope::Global))
		{
			Receiver = GlobalRouter.RegisterNewReceiver(InDataTypeName, InAddress.ChannelName, InitParams);
		}
		else if (FSubsystemData* FoundSubsystem = SubsystemRouters.Find(InAddress.Subsystem))
		{
			ITransmissionSubsystem* SubsystemInterface = FoundSubsystem->SubsystemPtr;

			// Ensure that this subsystem can support this send type.
			if (ensureAlways(SubsystemInterface && SubsystemInterface->CanSupportDataType(InDataTypeName)))
			{
				FScopeLock ScopeLock(&SubsystemRoutersMutationLock);

				Receiver = FoundSubsystem->AddressRouter.RegisterNewReceiver(InDataTypeName, InAddress.ChannelName, InitParams);
				FoundSubsystem->SubsystemPtr->OnNewReceiverRegistered(InAddress, InDataTypeName);
			}
		}
		else
		{
			// Otherwise, the subsystem FName was invalid.
			UE_LOG(LogMetaSound, Error, TEXT("Cannot create Receiver. Did not find transmission subsystem [Name:%s]"), *InAddress.Subsystem.ToString());
		}

		return MoveTemp(Receiver);
	}

	bool FDataTransmissionCenter::UnregisterDataChannel(const FName& InDataTypeName, const FSendAddress& InAddress)
	{
		// Only supporting global router. Other subsystems are not used and will be reworked. 
		if (ensure(InAddress.Subsystem == GetSubsystemNameForSendScope(ETransmissionScope::Global)))
		{
			return GlobalRouter.UnregisterDataChannel(InDataTypeName, InAddress.ChannelName);
		}
		return false;
	}

	bool FDataTransmissionCenter::UnregisterDataChannelIfUnconnected(const FName& InDataTypeName, const FSendAddress& InAddress)
	{
		// Only supporting global router. Other subsystems are not used and will be reworked. 
		if (ensure(InAddress.Subsystem == GetSubsystemNameForSendScope(ETransmissionScope::Global)))
		{
			return GlobalRouter.UnregisterDataChannelIfUnconnected(InDataTypeName, InAddress.ChannelName);
		}
		return false;
	}

	bool FDataTransmissionCenter::PushLiteral(FName DataType, FName GlobalChannelName, const FLiteral& InParam)
	{
		return GlobalRouter.PushLiteral(DataType, GlobalChannelName, InParam);
	}

	void FDataTransmissionCenter::RegisterSubsystem(ITransmissionSubsystem* InSystem, FName InSubsytemName)
	{
		FScopeLock ScopeLock(&SubsystemRoutersMutationLock);

		//check to make sure we're not adding a subsystem twice.
		check(!SubsystemRouters.Contains(InSubsytemName));

		SubsystemRouters.Emplace(InSubsytemName, InSystem);
	}

	void FDataTransmissionCenter::UnregisterSubsystem(FName InSubsystemName)
	{
		FScopeLock ScopeLock(&SubsystemRoutersMutationLock);

		//check to make sure we're not adding a subsystem twice.
		check(SubsystemRouters.Contains(InSubsystemName));

		SubsystemRouters.Remove(InSubsystemName);
	}

	FAddressRouter::FDataChannelKey FAddressRouter::GetDataChannelKey(const FName& InDataTypeName, const FName& InChannelName) const
	{
		return FString::Format(TEXT("{0}[{1}]"), { InChannelName.ToString(), InDataTypeName.ToString() });
	}

	TSharedPtr<IDataChannel, ESPMode::ThreadSafe> FAddressRouter::FindDataChannel(const FName& InDataTypeName, const FName& InChannelName)
	{
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> Channel;

		const FDataChannelKey ChannelKey = GetDataChannelKey(InDataTypeName, InChannelName);

		{
			FScopeLock ScopeLock(&DataChannelMapMutationLock);

			if (TSharedRef<IDataChannel, ESPMode::ThreadSafe>* ExistingChannelPtr = DataChannelMap.Find(ChannelKey))
			{
				Channel = *ExistingChannelPtr;
			}
		}

		return Channel;
	}

	TSharedPtr<IDataChannel, ESPMode::ThreadSafe> FAddressRouter::GetDataChannel(const FName& InDataTypeName, const FName& InChannelName, const FOperatorSettings& InOperatorSettings)
	{
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> DataChannel = FindDataChannel(InDataTypeName, InChannelName);

		if (!DataChannel.IsValid())
		{
			FScopeLock ScopeLock(&DataChannelMapMutationLock);

			const FDataChannelKey ChannelKey = GetDataChannelKey(InDataTypeName, InChannelName);
			
			// This is the first time we're seeing this, add it to the map.
			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

			if (ensure(nullptr != Registry))
			{
				DataChannel = Registry->CreateDataChannelForDataType(InDataTypeName, InOperatorSettings);
				if (DataChannel.IsValid())
				{
					DataChannelMap.Add(ChannelKey, DataChannel.ToSharedRef());
				}
			}
		}

		return DataChannel;
	}

	TUniquePtr<ISender> FAddressRouter::RegisterNewSender(const FName& InDataTypeName, const FName& InChannelName, const FSenderInitParams& InitParams)
	{

		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> DataChannel = GetDataChannel(InDataTypeName, InChannelName, InitParams.OperatorSettings);

		if (DataChannel.IsValid())
		{
			return DataChannel->NewSender(InitParams);
		}
		else
		{
			return TUniquePtr<ISender>(nullptr);
		}
	}

	bool FAddressRouter::UnregisterDataChannel(const FName& InDataTypeName, const FName& InChannelName)
	{
		FScopeLock ScopeLock(&DataChannelMapMutationLock);
		const FDataChannelKey ChannelKey = GetDataChannelKey(InDataTypeName, InChannelName);

		if (TSharedRef<IDataChannel, ESPMode::ThreadSafe>* Channel = DataChannelMap.Find(ChannelKey))
		{
			if (const int32 NumReceiversActive = Channel->Get().GetNumActiveReceivers())
			{
				UE_LOG(LogMetaSound, Verbose, TEXT("DataChannel '%s' of type '%s' shutting down with %d receivers active."), *InChannelName.ToString(), *InDataTypeName.ToString(), NumReceiversActive);
			}

			if (const int32 NumSendersActive = Channel->Get().GetNumActiveSenders())
			{
				UE_LOG(LogMetaSound, Verbose, TEXT("DataChannel '%s' of type '%s' shutting down with %d senders active."), *InChannelName.ToString(), *InDataTypeName.ToString(), NumSendersActive);
			}
		}

		return DataChannelMap.Remove(ChannelKey) > 0;
	}

	bool FAddressRouter::UnregisterDataChannelIfUnconnected(const FName& InDataTypeName, const FName& InChannelName)
	{
		FScopeLock ScopeLock(&DataChannelMapMutationLock);
		const FDataChannelKey ChannelKey = GetDataChannelKey(InDataTypeName, InChannelName);

		if (TSharedRef<IDataChannel, ESPMode::ThreadSafe>* Channel = DataChannelMap.Find(ChannelKey))
		{
			if (0 == Channel->Get().GetNumActiveReceivers())
			{
				if (0 == Channel->Get().GetNumActiveSenders())
				{
					return DataChannelMap.Remove(ChannelKey) > 0;
				}
			}
		}

		return false;
	}

	TUniquePtr<IReceiver> FAddressRouter::RegisterNewReceiver(const FName& InDataTypeName, const FName& InChannelName, const FReceiverInitParams& InitParams)
	{
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> DataChannel = GetDataChannel(InDataTypeName, InChannelName, InitParams.OperatorSettings);

		if (DataChannel.IsValid())
		{
			return DataChannel->NewReceiver(InitParams);
		}
		else
		{
			return TUniquePtr<IReceiver>(nullptr);
		}
	}

	TUniquePtr<IReceiver> FInstanceLocalRouter::RegisterNewReceiver(uint64 InInstanceID, const FName& InDataTypeName, const FName& InChannelName, const FReceiverInitParams& InitParams)
	{
		FScopeLock ScopeLock(&InstanceRouterMapMutationLock);

		if (FAddressRouter* AddressRouter = InstanceRouterMap.Find(InInstanceID))
		{
			return AddressRouter->RegisterNewReceiver(InDataTypeName, InChannelName, InitParams);
		}
		else
		{
			// This is the first time we're seeing this, add it to the map.
			FAddressRouter& NewRouter = InstanceRouterMap.Add(InInstanceID, FAddressRouter());
			return NewRouter.RegisterNewReceiver(InDataTypeName, InChannelName, InitParams);
		}
	}

	TUniquePtr<ISender> FInstanceLocalRouter::RegisterNewSender(uint64 InInstanceID, const FName& InDataTypeName, const FName& InChannelName, const FSenderInitParams& InitParams)
	{
		FScopeLock ScopeLock(&InstanceRouterMapMutationLock);

		if (FAddressRouter* AddressRouter = InstanceRouterMap.Find(InInstanceID))
		{
			return AddressRouter->RegisterNewSender(InDataTypeName, InChannelName, InitParams);
		}
		else
		{
			// This is the first time we're seeing this, add it to the map.
			FAddressRouter& NewRouter = InstanceRouterMap.Add(InInstanceID, FAddressRouter());
			return NewRouter.RegisterNewSender(InDataTypeName, InChannelName, InitParams);
		}
	}
}
