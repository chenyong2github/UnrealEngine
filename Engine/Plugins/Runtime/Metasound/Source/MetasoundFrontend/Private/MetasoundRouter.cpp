// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundRouter.h"

#include "MetasoundFrontendRegistries.h"
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

			Metasound::FLiteral LiteralParam(ValueToPush);

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

			Metasound::FLiteral LiteralParam(ValueToPush);

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

			Metasound::FLiteral LiteralParam(ValueToPush);

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

			Metasound::FLiteral LiteralParam(Args[1]);

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
			UE_LOG(LogMetasound, Error, TEXT("Cannot create Sender. Did not find transmission subsystem [Name:%s]"), *InAddress.Subsystem.ToString());
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
			UE_LOG(LogMetasound, Error, TEXT("Cannot create Receiver. Did not find transmission subsystem [Name:%s]"), *InAddress.Subsystem.ToString());
		}

		return MoveTemp(Receiver);
	}

	bool FDataTransmissionCenter::PushLiteral(FName GlobalChannelName, const FLiteral& InParam)
	{
		return GlobalRouter.PushLiteral(GlobalChannelName, InParam);
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

		if (TSharedRef<IDataChannel, ESPMode::ThreadSafe>* FoundChannel = DataChannelMap.Find(InChannelName))
		{
			return (*FoundChannel)->GetDataType();
		}
		else
		{
			return FName();
		}
	}

	TSharedPtr<IDataChannel, ESPMode::ThreadSafe> FAddressRouter::GetDataChannel(const FName& InDataTypeName, const FName& InChannelName, const FOperatorSettings& InOperatorSettings)
	{
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> DataChannel; 

		{
			FScopeLock ScopeLock(&DataChannelMapMutationLock);

			if (TSharedRef<IDataChannel, ESPMode::ThreadSafe>* ExistingChannelPtr = DataChannelMap.Find(InChannelName))
			{
				DataChannel = *ExistingChannelPtr;
			}
			else
			{
				// This is the first time we're seeing this, add it to the map.
				FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

				if (ensure(nullptr != Registry))
				{
					DataChannel = Registry->CreateDataChannelForDataType(InDataTypeName, InOperatorSettings);
					if (DataChannel.IsValid())
					{
						DataChannelMap.Add(InChannelName, DataChannel.ToSharedRef());
					}
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
