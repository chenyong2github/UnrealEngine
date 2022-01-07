// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXOutputPort.h"

#include "DMXProtocolLog.h"
#include "DMXProtocolSettings.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXSender.h"
#include "IO/DMXOutputPortConfig.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXRawListener.h"

#include "HAL/IConsoleManager.h"
#include "HAL/RunnableThread.h"


#define LOCTEXT_NAMESPACE "DMXOutputPort"

/** Helper to override a member variable of an Output Port */
#define DMX_OVERRIDE_OUTPUTPORT_VAR(MemberName, PortName, Value) \
{ \
	const FDMXOutputPortSharedRef* OutputPortPtr = FDMXPortManager::Get().GetOutputPorts().FindByPredicate([PortName](const FDMXOutputPortSharedPtr& OutputPort) \
		{ \
			return OutputPort->GetPortName() == PortName; \
		}); \
	if (OutputPortPtr) \
	{ \
		const FDMXOutputPortConfig OldOutputPortConfig = (*OutputPortPtr)->MakeOutputPortConfig(); \
		FDMXOutputPortConfigParams OutputPortConfigParams = FDMXOutputPortConfigParams(OldOutputPortConfig); \
		OutputPortConfigParams.MemberName = Value; \
		FDMXOutputPortConfig NewOutputPortConfig((*OutputPortPtr)->GetPortGuid(), OutputPortConfigParams); \
		constexpr bool bForceUpdateRegistrationWithProtocol = true; \
		(*OutputPortPtr)->UpdateFromConfig(NewOutputPortConfig, bForceUpdateRegistrationWithProtocol); \
	} \
}

static FAutoConsoleCommand GDMXSetOutputPortProtocolCommand(
	TEXT("DMX.SetOutputPortProtocol"),
	TEXT("DMX.SetOutputPortProtocol [PortName][ProtocolName]. Sets the protocol used by the output port. Example: DMX.SetOutputPortProtocol MyOutputPort Art-Net"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FName ProtocolNameValue = FName(Args[1]);

			if (IDMXProtocol::GetProtocolNames().Contains(ProtocolNameValue))
			{
				DMX_OVERRIDE_OUTPUTPORT_VAR(ProtocolName, PortName, ProtocolNameValue);
			}
		})
);

static FAutoConsoleCommand GDMXSetOutputPortCommunicationTypeCommand(
	TEXT("DMX.SetOutputPortCommunicationType"),
	TEXT("DMX.SetOutputPortCommunicationType [PortName][CommunicationType (0 = Broadcast, 1 = Unicast, 2 = Multicast)]. Sets the communication type of an output port. Example: DMX.SetOutputPortCommunicationType MyOutputPort 2"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FString& CommunicationTypeValueString = Args[1];
			uint8 CommunicationTypeValue;
			if (LexTryParseString<uint8>(CommunicationTypeValue, *CommunicationTypeValueString))
			{
				const EDMXCommunicationType CommunicationTypeEnumValue = static_cast<EDMXCommunicationType>(CommunicationTypeValue);
				if (CommunicationTypeEnumValue == EDMXCommunicationType::Broadcast ||
					CommunicationTypeEnumValue == EDMXCommunicationType::Multicast ||
					CommunicationTypeEnumValue == EDMXCommunicationType::Unicast)
				{
					const FDMXOutputPortSharedRef* PortPtr = FDMXPortManager::Get().GetOutputPorts().FindByPredicate([PortName](const FDMXOutputPortSharedPtr& OutputPort)
						{
							return OutputPort->GetPortName() == PortName;
						});

					if (PortPtr)
					{
						if (const IDMXProtocolPtr Protocol = (*PortPtr)->GetProtocol())
						{
							if (Protocol->GetOutputPortCommunicationTypes().Contains(CommunicationTypeEnumValue))
							{
								DMX_OVERRIDE_OUTPUTPORT_VAR(CommunicationType, PortName, CommunicationTypeEnumValue);
							}
						}
					}
				}
			}
		})
);


static FAutoConsoleCommand GDMXSetOutputPortOutputPortDeviceAddressCommand(
	TEXT("DMX.SetOutputPortDeviceAddress"),
	TEXT("DMX.SetOutputPortDeviceAddress [PortName][DeviceAddress]. Sets the Device Address of an output port, usually the network interface card IP address. Example: DMX.SetInputPortDeviceAddress MyOutputPort 123.45.67.89"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FString& DeviceAddressValue = Args[1];

			DMX_OVERRIDE_OUTPUTPORT_VAR(DeviceAddress, PortName, DeviceAddressValue);
		})
);

static FAutoConsoleCommand GDMXSetOutputPortOutputPortDestinationAddressesCommand(
	TEXT("DMX.SetOutputPortDestinationAddresses"),
	TEXT("DMX.SetOutputPortDestinationAddresses [PortName][DestinationAddress1][DestinationAddress2][...][DestinationAddressN]. Sets the Destination Addresses of an output port. Example: DMX.SetInputPortDeviceAddress MyOutputPort 11.33.55.77 22.44.66.88"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			TArray<FDMXOutputPortDestinationAddress>  DestinationAddressesValue;
			for (int32 ArgIndex = 1; ArgIndex < Args.Num(); ArgIndex++)
			{
				const FDMXOutputPortDestinationAddress Address = FDMXOutputPortDestinationAddress(Args[ArgIndex]);
				DestinationAddressesValue.Add(Address);
			}

			DMX_OVERRIDE_OUTPUTPORT_VAR(DestinationAddresses, PortName, DestinationAddressesValue);
		})
);

static FAutoConsoleCommand GDMXSetOutputPortLoopbackToEngineCommand(
	TEXT("DMX.SetOutputPortInputIntoEngine"),
	TEXT("DMX.SetOutputPortInputIntoEngine [PortName][Flag]. Sets if the Output Port is input into the engine directly. Example: DMX.SetOutputPortInputIntoEngine MyOutputPort 1"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const bool bLoopbackToEngine = Args[1] == TEXT("1") || Args[1].Equals(TEXT("true"), ESearchCase::IgnoreCase);

			DMX_OVERRIDE_OUTPUTPORT_VAR(bLoopbackToEngine, PortName, bLoopbackToEngine);
		})
);

static FAutoConsoleCommand GDMXSetOutputPortLocalUniverseStartCommand(
	TEXT("DMX.SetOutputPortLocalUniverseStart"),
	TEXT("DMX.SetOutputPortLocalUniverseStart [PortName][Universe]. Sets the local universe start of the output port. Example: DMX.SetOutputPortLocalUniverseStart MyOutputPort 5"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FString& LocalUniverseStartValueString = Args[1];
			int32 LocalUniverseStartValue;
			if (LexTryParseString<int32>(LocalUniverseStartValue, *LocalUniverseStartValueString))
			{
				if (LocalUniverseStartValue >= 0 && LocalUniverseStartValue <= DMX_MAX_UNIVERSE)
				{
					DMX_OVERRIDE_OUTPUTPORT_VAR(LocalUniverseStart, PortName, LocalUniverseStartValue);
				}
			}
		})
);

static FAutoConsoleCommand GDMXSetOutputPortNumUniversesCommand(
	TEXT("DMX.SetOutputPortNumUniverses"),
	TEXT("DMX.SetOutputPortNumUniverses [PortName][Universe]. Sets the num universes of the output port. Example: DMX.SetOutputPortNumUniverses MyOutputPort 10"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FString& NumUniversesValueString = Args[1];
			int32 NumUniversesValue;
			if (LexTryParseString<int32>(NumUniversesValue, *NumUniversesValueString))
			{
				if (NumUniversesValue >= 0 && NumUniversesValue <= DMX_MAX_UNIVERSE)
				{
					DMX_OVERRIDE_OUTPUTPORT_VAR(NumUniverses, PortName, NumUniversesValue);
				}
			}
		})
);

static FAutoConsoleCommand GDMXSetOutputPortExternUniverseStartCommand(
	TEXT("DMX.SetOutputPortExternUniverseStart"),
	TEXT("DMX.SetOutputPortExternUniverseStart [PortName][Universe]. Sets the extern universe start of the output port. Example: DMX.SetOutputPortExternUniverseStart MyOutputPort 7"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FString& ExternUniverseStartValueString = Args[1];
			int32 ExternUniverseStartValue;
			if (LexTryParseString<int32>(ExternUniverseStartValue, *ExternUniverseStartValueString))
			{
				if (ExternUniverseStartValue >= 0 && ExternUniverseStartValue <= DMX_MAX_UNIVERSE)
				{
					DMX_OVERRIDE_OUTPUTPORT_VAR(ExternUniverseStart, PortName, ExternUniverseStartValue);
				}
			}
		})
);

static FAutoConsoleCommand GDMXSetOutputPortPriorityCommand(
	TEXT("DMX.SetOutputPortPriority"),
	TEXT("DMX.SetOutputPortPriority [PortName][Priority]. Sets the priority of the output port. Example: DMX.SetOutputPortPriority MyOutputPort 100"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FString& PriorityValueString = Args[1];
			int32 PriorityValue;
			if (LexTryParseString<int32>(PriorityValue, *PriorityValueString))
			{
				DMX_OVERRIDE_OUTPUTPORT_VAR(Priority, PortName, PriorityValue);
			}
		})
);

static FAutoConsoleCommand GDMXSetOutputPortDelayCommand(
	TEXT("DMX.SetOutputPortDelay"),
	TEXT("DMX.SetOutputPortDelay [PortName][Delay]. Sets the delay of the output port (depending on the delay frame rate). Example: DMX.SetOutputPortDelay MyOutputPort 3.54"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FString& DelayValueString = Args[1];
			double DelayValue;
			if (LexTryParseString<double>(DelayValue, *DelayValueString))
			{
				if (DelayValue >= 0.0)
				{
					const FDMXOutputPortSharedRef* PortPtr = FDMXPortManager::Get().GetOutputPorts().FindByPredicate([PortName](const FDMXOutputPortSharedPtr& OutputPort)
						{
							return OutputPort->GetPortName() == PortName;
						});

					if (PortPtr)
					{
						const FDMXOutputPortConfig PortConfig = (*PortPtr)->MakeOutputPortConfig();
						const double DelaySeconds = FMath::Min(DelayValue * PortConfig.GetDelayFrameRate().AsDecimal(), 60.f);

						DMX_OVERRIDE_OUTPUTPORT_VAR(DelaySeconds, PortName, DelayValue);
					}
				}
			}
		})
);

static FAutoConsoleCommand GDMXSetOutputPortDelayFrameRateCommand(
	TEXT("DMX.SetOutputPortDelayFrameRate"),
	TEXT("DMX.SetOutputPortDelayFrameRate [PortName][FrameRate]. Sets the frame rate of the delay of the output port (1.0 for seconds). Example: DMX.SetOutputPortDelayFrameRate MyOutputPort 33.3"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FString& DelayFrameRateValueString = Args[1];
			double DelayFrameRateValue;
			if (LexTryParseString<double>(DelayFrameRateValue, *DelayFrameRateValueString))
			{
				if (DelayFrameRateValue > 0.0)
				{
					const FDMXOutputPortSharedRef* PortPtr = FDMXPortManager::Get().GetOutputPorts().FindByPredicate([PortName](const FDMXOutputPortSharedPtr& OutputPort)
						{
							return OutputPort->GetPortName() == PortName;
						});

					if (PortPtr)
					{
						const FDMXOutputPortConfig PortConfig = (*PortPtr)->MakeOutputPortConfig();
						const double OldDelay = PortConfig.GetDelaySeconds() / PortConfig.GetDelayFrameRate().AsDecimal();
						
						const FFrameRate NewDelayFrameRate = FFrameRate(1, DelayFrameRateValue);
						const double NewDelaySeconds = FMath::Min(OldDelay * NewDelayFrameRate.AsDecimal(), 60.f);

						DMX_OVERRIDE_OUTPUTPORT_VAR(DelayFrameRate, PortName, NewDelayFrameRate);
						DMX_OVERRIDE_OUTPUTPORT_VAR(DelaySeconds, PortName, NewDelaySeconds);
					}
				}
			}
		})
);

static FAutoConsoleCommand GDMXResetOutputPortToProjectSettings(
	TEXT("DMX.ResetOutputPortToProjectSettings"),
	TEXT("DMX.ResetOutputPortToProjectSettings [PortName]. Resets the output port to how it is defined in project settings. Example: DMX.ResetOutputPortToProjectSettings MyOutputPort"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				return;
			}

			const FString& PortName = Args[0];
			const FDMXOutputPortSharedRef* PortPtr = FDMXPortManager::Get().GetOutputPorts().FindByPredicate([PortName](const FDMXOutputPortSharedPtr& OutputPort)
				{
					return OutputPort->GetPortName() == PortName;
				});

			if (PortPtr)
			{
				const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
				if (ProtocolSettings)
				{
					const FDMXOutputPortConfig* PortConfigPtr = ProtocolSettings->OutputPortConfigs.FindByPredicate([PortPtr](const FDMXOutputPortConfig& OutputPortConfig)
						{
							return OutputPortConfig.GetPortGuid() == (*PortPtr)->GetPortGuid();
						});

					if (PortConfigPtr)
					{
						FDMXOutputPortConfig PortConfig = *PortConfigPtr;
						(*PortPtr)->UpdateFromConfig(PortConfig);
					}
				}
			}
		})
);

#undef DMX_OVERRIDE_OUTPUTPORT_VAR


FDMXOutputPortSharedRef FDMXOutputPort::CreateFromConfig(FDMXOutputPortConfig& OutputPortConfig)
{
	// Port Configs are expected to have a valid guid always
	check(OutputPortConfig.GetPortGuid().IsValid());

	FDMXOutputPortSharedRef NewOutputPort = MakeShared<FDMXOutputPort, ESPMode::ThreadSafe>();

	NewOutputPort->PortGuid = OutputPortConfig.GetPortGuid();

	UDMXProtocolSettings* Settings = GetMutableDefault<UDMXProtocolSettings>();
	check(Settings);

	NewOutputPort->CommunicationDeterminator.SetSendEnabled(Settings->IsSendDMXEnabled());
	NewOutputPort->CommunicationDeterminator.SetReceiveEnabled(Settings->IsReceiveDMXEnabled());

	Settings->OnSetSendDMXEnabled.AddThreadSafeSP(NewOutputPort, &FDMXOutputPort::OnSetSendDMXEnabled);
	Settings->OnSetReceiveDMXEnabled.AddThreadSafeSP(NewOutputPort, &FDMXOutputPort::OnSetReceiveDMXEnabled);

	NewOutputPort->UpdateFromConfig(OutputPortConfig);

	const FString SenderThreadName = FString(TEXT("DMXOutputPort_")) + OutputPortConfig.GetPortName();
	NewOutputPort->Thread = FRunnableThread::Create(&NewOutputPort.Get(), *SenderThreadName, 0U, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Created output port %s"), *NewOutputPort->PortName);

	return NewOutputPort;
}

FDMXOutputPort::~FDMXOutputPort()
{	
	// All Inputs need to be explicitly removed before destruction 
	check(RawListeners.Num() == 0);
	
	// Port needs be unregistered before destruction
	check(DMXSenderArray.Num() == 0);

	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;
	}

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Destroyed output port %s"), *PortName);
}

FDMXOutputPortConfig FDMXOutputPort::MakeOutputPortConfig() const
{
	FDMXOutputPortConfigParams Params;
	Params.PortName = PortName;
	Params.ProtocolName = Protocol.IsValid() ? Protocol->GetProtocolName() : NAME_None;
	Params.CommunicationType = CommunicationType;
	Params.DeviceAddress = DeviceAddress;
	Params.DestinationAddresses = DestinationAddresses;
	Params.bLoopbackToEngine = CommunicationDeterminator.IsLoopbackToEngineEnabled();
	Params.LocalUniverseStart = LocalUniverseStart;
	Params.NumUniverses = NumUniverses;
	Params.ExternUniverseStart = ExternUniverseStart;
	Params.Priority = Priority;
	Params.DelaySeconds = DelaySeconds;
	Params.DelayFrameRate = DelayFrameRate;

	return FDMXOutputPortConfig(PortGuid, Params);
}

void FDMXOutputPort::UpdateFromConfig(FDMXOutputPortConfig& InOutOutputPortConfig, bool bForceUpdateRegistrationWithProtocol)
{	
	// Need a valid config for the port
	InOutOutputPortConfig.MakeValid();

	// Avoid further changes to the config
	const FDMXOutputPortConfig& OutputPortConfig = InOutOutputPortConfig;

	// Can only use configs that correspond to project settings
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	const bool bConfigIsInProjectSettings = ProtocolSettings->OutputPortConfigs.ContainsByPredicate([&OutputPortConfig](const FDMXOutputPortConfig& Other) {
		return OutputPortConfig.GetPortGuid() == Other.GetPortGuid();
	});
	checkf(bConfigIsInProjectSettings, TEXT("Can only use configs with a guid that corresponds to a config in project settings"));

	// Find if the port needs update its registration with the protocol
	const bool bNeedsUpdateRegistration = [this, &OutputPortConfig, bForceUpdateRegistrationWithProtocol]()
	{
		if (bForceUpdateRegistrationWithProtocol)
		{
			return true;
		}

		if (IsRegistered() != CommunicationDeterminator.IsSendDMXEnabled())
		{
			return true;
		}

		FName ProtocolName = Protocol.IsValid() ? Protocol->GetProtocolName() : NAME_None;

		if (ProtocolName == OutputPortConfig.GetProtocolName() &&
			DeviceAddress == OutputPortConfig.GetDeviceAddress() &&
			DestinationAddresses == OutputPortConfig.GetDestinationAddresses() &&
			CommunicationType == OutputPortConfig.GetCommunicationType() &&
			Priority == OutputPortConfig.GetPriority() &&
			DelaySeconds == OutputPortConfig.GetDelaySeconds())
		{
			return false;
		}	

		return true;
	}();

	// Unregister the port if required before the new protocol is set
	if (bNeedsUpdateRegistration)
	{
		if (IsRegistered())
		{
			Unregister();
		}
	}

	Protocol = IDMXProtocol::Get(OutputPortConfig.GetProtocolName());

	// Copy properties from the config
	const FGuid& ConfigPortGuid = OutputPortConfig.GetPortGuid();
	check(PortGuid.IsValid());
	PortGuid = ConfigPortGuid;

	CommunicationType = OutputPortConfig.GetCommunicationType();
	ExternUniverseStart = OutputPortConfig.GetExternUniverseStart();
	DeviceAddress = OutputPortConfig.GetDeviceAddress();
	DestinationAddresses = OutputPortConfig.GetDestinationAddresses();
	LocalUniverseStart = OutputPortConfig.GetLocalUniverseStart();
	NumUniverses = OutputPortConfig.GetNumUniverses();
	PortName = OutputPortConfig.GetPortName();
	Priority = OutputPortConfig.GetPriority();
	DelaySeconds = OutputPortConfig.GetDelaySeconds();
	DelayFrameRate = OutputPortConfig.GetDelayFrameRate();

	CommunicationDeterminator.SetLoopbackToEngine(OutputPortConfig.NeedsLoopbackToEngine());

	// Re-register the port if required
	if (bNeedsUpdateRegistration)
	{
		Register();
	}

	OnPortUpdated.Broadcast();
}

const FGuid& FDMXOutputPort::GetPortGuid() const
{
	check(PortGuid.IsValid());
	return PortGuid;
}

void FDMXOutputPort::ClearBuffers()
{
	check(IsInGameThread());

	for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
	{
		RawListener->ClearBuffer();
	}

	// Clear gamethread buffer
	ExternUniverseToLatestSignalMap_GameThread.Reset();

	// Clear port thread buffers
	FScopeLock LockClearBuffers(&ClearBuffersCriticalSection);
	SignalFragments.Empty();
	ExternUniverseToLatestSignalMap_PortThread.Reset();
}

bool FDMXOutputPort::IsLoopbackToEngine() const
{
	return CommunicationDeterminator.NeedsLoopbackToEngine();
}

TArray<FString> FDMXOutputPort::GetDestinationAddresses() const
{
	TArray<FString> Result;
	Result.Reserve(DestinationAddresses.Num());
	for (const FDMXOutputPortDestinationAddress& DestinationAddress : DestinationAddresses)
	{
		Result.Add(DestinationAddress.DestinationAddressString);
	}

	return Result;
}

bool FDMXOutputPort::GameThreadGetDMXSignal(int32 LocalUniverseID, FDMXSignalSharedPtr& OutDMXSignal, bool bEvenIfNotLoopbackToEngine)
{
	check(IsInGameThread());

	const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();
	if (bNeedsLoopbackToEngine || bEvenIfNotLoopbackToEngine)
	{
		int32 ExternUniverseID = ConvertLocalToExternUniverseID(LocalUniverseID);

		const FDMXSignalSharedPtr* SignalPtr = ExternUniverseToLatestSignalMap_GameThread.Find(ExternUniverseID);
		if (SignalPtr)
		{
			OutDMXSignal = *SignalPtr;
			return true;
		}
	}

	return false;
}

bool FDMXOutputPort::GameThreadGetDMXSignalFromRemoteUniverse(FDMXSignalSharedPtr& OutDMXSignal, int32 RemoteUniverseID, bool bEvenIfNotLoopbackToEngine)
{
	// DEPRECATED 4.27
	check(IsInGameThread());

	const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();
	if (bNeedsLoopbackToEngine || bEvenIfNotLoopbackToEngine)
	{
		const FDMXSignalSharedPtr* SignalPtr = ExternUniverseToLatestSignalMap_GameThread.Find(RemoteUniverseID);
		if (SignalPtr)
		{
			OutDMXSignal = *SignalPtr;
			return true;
		}
	}

	return false;
}

FString FDMXOutputPort::GetDestinationAddress() const
{
	// DEPRECATED 5.0
	return DestinationAddresses.Num() > 0 ? DestinationAddresses[0].DestinationAddressString : TEXT("");
}

bool FDMXOutputPort::IsRegistered() const
{
	if (DMXSenderArray.Num() > 0)
	{
		return true;
	}

	return false;
}

void FDMXOutputPort::AddRawListener(TSharedRef<FDMXRawListener> InRawListener)
{
	check(!RawListeners.Contains(InRawListener));

	// Inputs need to run in the game thread
	check(IsInGameThread());

	RawListeners.Add(InRawListener);
}

void FDMXOutputPort::RemoveRawListener(TSharedRef<FDMXRawListener> InRawListenerToRemove)
{
	RawListeners.Remove(InRawListenerToRemove);
}

void FDMXOutputPort::SendDMX(int32 LocalUniverseID, const TMap<int32, uint8>& ChannelToValueMap)
{
	check(IsInGameThread());

	if (IsLocalUniverseInPortRange(LocalUniverseID))
	{
		const bool bNeedsSendDMX = CommunicationDeterminator.NeedsSendDMX();
		const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();

		// Update the buffer for loopback if dmx needs be sent and/or looped back
		if (bNeedsSendDMX || bNeedsLoopbackToEngine)
		{
			const int32 ExternUniverseID = ConvertLocalToExternUniverseID(LocalUniverseID);

			const double SendTime = FPlatformTime::Seconds() + DelaySeconds;

			// Enqueue for this port's thread
			const TSharedPtr<FDMXSignalFragment> Fragment = MakeShared<FDMXSignalFragment>(ExternUniverseID, ChannelToValueMap, SendTime);
			SignalFragments.Enqueue(Fragment);

			if (bNeedsLoopbackToEngine)
			{
				// Write the fragment to the game thread's buffer
				const FDMXSignalSharedPtr& Signal = ExternUniverseToLatestSignalMap_GameThread.FindOrAdd(ExternUniverseID, MakeShared<FDMXSignal, ESPMode::ThreadSafe>());

				for (const TTuple<int32, uint8>& ChannelValueKvp : ChannelToValueMap)
				{
					int32 ChannelIndex = ChannelValueKvp.Key - 1;

					// Filter invalid indicies so we can send bp calls here without testing them first.
					if (Signal->ChannelData.IsValidIndex(ChannelIndex))
					{
						Signal->Timestamp = SendTime;
						Signal->ChannelData[ChannelIndex] = ChannelValueKvp.Value;
					}
				}
			}
		}
	}
}

void FDMXOutputPort::SendDMXToRemoteUniverse(const TMap<int32, uint8>& ChannelToValueMap, int32 RemoteUniverse)
{
	// DEPRECATED 4.27
	check(IsInGameThread());

	if (IsExternUniverseInPortRange(RemoteUniverse))
	{
		const bool bNeedsSendDMX = CommunicationDeterminator.NeedsSendDMX();
		const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();

		// Update the buffer for loopback if dmx needs be sent and/or looped back
		if (bNeedsSendDMX || bNeedsLoopbackToEngine)
		{
			const double SendTime = FPlatformTime::Seconds() + DelaySeconds;

			// Enqueue for this port's thread
			const TSharedPtr<FDMXSignalFragment> Fragment = MakeShared<FDMXSignalFragment>(RemoteUniverse, ChannelToValueMap, SendTime);
			SignalFragments.Enqueue(Fragment);

			if (bNeedsLoopbackToEngine)
			{
				// Write the fragment to the game thread's buffer
				const FDMXSignalSharedPtr& Signal = ExternUniverseToLatestSignalMap_GameThread.FindOrAdd(RemoteUniverse, MakeShared<FDMXSignal, ESPMode::ThreadSafe>());

				for (const TTuple<int32, uint8>& ChannelValueKvp : ChannelToValueMap)
				{
					int32 ChannelIndex = ChannelValueKvp.Key - 1;

					// Filter invalid indicies so we can send bp calls here without testing them first.
					if (Signal->ChannelData.IsValidIndex(ChannelIndex))
					{
						Signal->Timestamp = SendTime;
						Signal->ChannelData[ChannelIndex] = ChannelValueKvp.Value;
					}
				}
			}
		}
	}
}

bool FDMXOutputPort::Register()
{
	if (Protocol.IsValid() && IsValidPortSlow() && CommunicationDeterminator.IsSendDMXEnabled() && !FDMXPortManager::Get().AreProtocolsSuspended())
	{
		FScopeLock LockAccessSenderArray(&AccessSenderArrayCriticalSection);
		DMXSenderArray = Protocol->RegisterOutputPort(SharedThis(this));

		if (DMXSenderArray.Num() > 0)
		{
			CommunicationDeterminator.SetHasValidSender(true);
			return true;
		}
	}

	CommunicationDeterminator.SetHasValidSender(false);
	return false;
}

void FDMXOutputPort::Unregister()
{
	if (IsRegistered())
	{
		if (Protocol.IsValid())
		{
			Protocol->UnregisterOutputPort(SharedThis(this));
		}

		FScopeLock LockAccessSenderArray(&AccessSenderArrayCriticalSection);
		DMXSenderArray.Reset();
	}

	CommunicationDeterminator.SetHasValidSender(false);
}

void FDMXOutputPort::OnSetSendDMXEnabled(bool bEnabled)
{
	CommunicationDeterminator.SetSendEnabled(bEnabled);

	FDMXOutputPortConfig Config = MakeOutputPortConfig();
	UpdateFromConfig(Config);
}

void FDMXOutputPort::OnSetReceiveDMXEnabled(bool bEnabled)
{
	CommunicationDeterminator.SetReceiveEnabled(bEnabled);

	FDMXOutputPortConfig Config = MakeOutputPortConfig();
	UpdateFromConfig(Config);
}

bool FDMXOutputPort::Init()
{
	return true;
}

uint32 FDMXOutputPort::Run()
{
	const UDMXProtocolSettings* DMXSettings = GetDefault<UDMXProtocolSettings>();
	check(DMXSettings);
	
	// Fixed rate delta time
	const double SendDeltaTime = 1.f / DMXSettings->SendingRefreshRate;

	while (!bStopping)
	{
		const double StartTime = FPlatformTime::Seconds();

		ProcessSendDMX();

		const double EndTime = FPlatformTime::Seconds();
		const double WaitTime = SendDeltaTime - (EndTime - StartTime);

		if (WaitTime > 0.f)
		{
			// Sleep by the amount which is set in refresh rate
			FPlatformProcess::SleepNoStats(WaitTime);
		}

		// In the unlikely case we took to long to send, we instantly continue, but do not take 
		// further measures to compensate - We would have to run faster than DMX send rate to catch up.
	}

	return 0;
}

void FDMXOutputPort::Stop()
{
	bStopping = true;
}

void FDMXOutputPort::Exit()
{
}

void FDMXOutputPort::Tick()
{
	ProcessSendDMX();
}

FSingleThreadRunnable* FDMXOutputPort::GetSingleThreadInterface()
{
	return this;
}

void FDMXOutputPort::ProcessSendDMX()
{
	// Delay signals
	const double Now = FPlatformTime::Seconds();

	FScopeLock LockClearBuffers(&ClearBuffersCriticalSection);
	
	// Write dmx fragments
	{
		for (;;)
		{
			TSharedPtr<FDMXSignalFragment, ESPMode::ThreadSafe> OldestFragment;
			if (SignalFragments.Peek(OldestFragment))
			{
				if (OldestFragment->SendTime <= Now)
				{
					const FDMXSignalSharedPtr& Signal = ExternUniverseToLatestSignalMap_PortThread.FindOrAdd(OldestFragment->ExternUniverseID, MakeShared<FDMXSignal, ESPMode::ThreadSafe>());

					// Write the fragment & meta data 
					for (const TTuple<int32, uint8>& ChannelValueKvp : OldestFragment->ChannelToValueMap)
					{
						int32 ChannelIndex = ChannelValueKvp.Key - 1;
						// Filter invalid indicies so we can send bp calls here without testing them first.
						if (Signal->ChannelData.IsValidIndex(ChannelIndex))
						{
							Signal->ChannelData[ChannelIndex] = ChannelValueKvp.Value;
						}
					}

					Signal->ExternUniverseID = OldestFragment->ExternUniverseID;
					Signal->Timestamp = Now;
					Signal->Priority = Priority;

					// Drop the written fragment
					SignalFragments.Pop();

					continue;
				}

				break;
			}

			break;
		}
	}

	// Send new and alive DMX Signals
	const bool bNeedsSendDMX = CommunicationDeterminator.NeedsSendDMX();
	for (const TTuple<int32, FDMXSignalSharedPtr>& UniverseToSignalPair : ExternUniverseToLatestSignalMap_PortThread)
	{
		if (UniverseToSignalPair.Value->Timestamp <= Now)
		{
			// Keeping the signal alive here:
			// Increment the timestamp by one second so the signal will be sent anew in one second.
			UniverseToSignalPair.Value->Timestamp = Now + 1.0;

			// Send via the protocol's sender
			if (bNeedsSendDMX)
			{
				FScopeLock LockAccessSenderArray(&AccessSenderArrayCriticalSection);
				for (const TSharedPtr<IDMXSender>& DMXSender : DMXSenderArray)
				{
					DMXSender->SendDMXSignal(UniverseToSignalPair.Value.ToSharedRef());
				}
			}

			// Loopback to Listeners
			for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
			{
				RawListener->EnqueueSignal(this, UniverseToSignalPair.Value.ToSharedRef());
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
