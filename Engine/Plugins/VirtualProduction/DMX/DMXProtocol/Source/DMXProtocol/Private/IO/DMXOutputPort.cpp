// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXOutputPort.h"

#include "DMXProtocolSettings.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXSender.h"
#include "IO/DMXOutputPortConfig.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXRawListener.h"


#define LOCTEXT_NAMESPACE "DMXOutputPort"


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

	// Bind to send dmx changes
	Settings->OnSetSendDMXEnabled.AddThreadSafeSP(NewOutputPort, &FDMXOutputPort::OnSetSendDMXEnabled);

	// Bind to receive dmx changes
	Settings->OnSetReceiveDMXEnabled.AddThreadSafeSP(NewOutputPort, &FDMXOutputPort::OnSetReceiveDMXEnabled);

	NewOutputPort->UpdateFromConfig(OutputPortConfig);

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Created output port %s"), *NewOutputPort->PortName);

	return NewOutputPort;
}

FDMXOutputPort::~FDMXOutputPort()
{	
	// All Inputs need to be explicitly removed before destruction 
	check(RawListeners.Num() == 0);
	
	// Port needs be unregistered before destruction
	check(!DMXSender);

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Destroyed output port %s"), *PortName);
}

void FDMXOutputPort::UpdateFromConfig(FDMXOutputPortConfig& OutputPortConfig)
{	
	// Need a valid config for the port
	OutputPortConfig.MakeValid();

	// Can only use configs that are in project settings
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	const bool bConfigIsInProjectSettings = ProtocolSettings->OutputPortConfigs.ContainsByPredicate([&OutputPortConfig](const FDMXOutputPortConfig& Other) {
		return OutputPortConfig.GetPortGuid() == Other.GetPortGuid();
	});
	checkf(bConfigIsInProjectSettings, TEXT("Can only use configs that are in project settings"));

	// Find if the port needs update its registration with the protocol
	const bool bNeedsUpdateRegistration = [this, &OutputPortConfig]()
	{
		if (IsRegistered() != CommunicationDeterminator.IsSendDMXEnabled())
		{
			return true;
		}

		FName ProtocolName = Protocol.IsValid() ? Protocol->GetProtocolName() : NAME_None;

		if (ProtocolName == OutputPortConfig.GetProtocolName() &&
			DeviceAddress == OutputPortConfig.GetDeviceAddress() &&
			DestinationAddress == OutputPortConfig.GetDestinationAddress() &&
			CommunicationType == OutputPortConfig.GetCommunicationType())
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
	DestinationAddress = OutputPortConfig.GetDestinationAddress();
	LocalUniverseStart = OutputPortConfig.GetLocalUniverseStart();
	NumUniverses = OutputPortConfig.GetNumUniverses();
	PortName = OutputPortConfig.GetPortName();
	Priority = OutputPortConfig.GetPriority();

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

bool FDMXOutputPort::IsRegistered() const
{
	if (DMXSender)
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
	if (IsLocalUniverseInPortRange(LocalUniverseID))
	{
		const bool bNeedsSendDMX = CommunicationDeterminator.NeedsSendDMX();
		const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();

		// Update the buffer for loopback if dmx needs be sent and/or looped back
		if (bNeedsSendDMX || bNeedsLoopbackToEngine)
		{
			const int32 ExternUniverseID = ConvertLocalToExternUniverseID(LocalUniverseID);

			FDMXSignalSharedPtr& ExistingOrNewSignal = ExternUniverseToLatestSignalMap.FindOrAdd(ExternUniverseID);
			if (!ExistingOrNewSignal.IsValid())
			{
				// Only create a new signal, initialization happens below
				ExistingOrNewSignal = MakeShared<FDMXSignal, ESPMode::ThreadSafe>();
			}
			FDMXSignalSharedRef Signal = ExistingOrNewSignal.ToSharedRef();

			// Init or update the signal
			// Write the fragment & meta data 
			for (const TTuple<int32, uint8>& ChannelValueKvp : ChannelToValueMap)
			{
				int32 ChannelIndex = ChannelValueKvp.Key - 1;

				// Filter invalid indicies so we can send bp calls here without testing them first.
				if (Signal->ChannelData.IsValidIndex(ChannelIndex))
				{
					Signal->ChannelData[ChannelIndex] = ChannelValueKvp.Value;
				}
			}	

			Signal->ExternUniverseID = ExternUniverseID;
			Signal->Timestamp = FPlatformTime::Seconds();
			Signal->Priority = Priority;

			// Send DMX
			if (bNeedsSendDMX)
			{
				DMXSender->SendDMXSignal(Signal);
			}

			// Loopback to Listeners
			for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
			{
				RawListener->EnqueueSignal(this, Signal);
			}
		}
	}
}

void FDMXOutputPort::SendDMXToRemoteUniverse(const TMap<int32, uint8>& ChannelToValueMap, int32 RemoteUniverse)
{
	// DEPRECATED 4.27
	if (IsExternUniverseInPortRange(RemoteUniverse))
	{
		const bool bNeedsSendDMX = CommunicationDeterminator.NeedsSendDMX();
		const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();

		// Update the buffer for loopback if dmx needs be sent and/or looped back
		if (bNeedsSendDMX || bNeedsLoopbackToEngine)
		{
			FDMXSignalSharedPtr& ExistingOrNewSignal = ExternUniverseToLatestSignalMap.FindOrAdd(RemoteUniverse);
			if (!ExistingOrNewSignal.IsValid())
			{
				// Only create a new signal, initialization happens below
				ExistingOrNewSignal = MakeShared<FDMXSignal, ESPMode::ThreadSafe>();
			}
			FDMXSignalSharedRef Signal = ExistingOrNewSignal.ToSharedRef();

			// Init or update the signal
			// Write the fragment & meta data 
			for (const TTuple<int32, uint8>& ChannelValueKvp : ChannelToValueMap)
			{
				int32 ChannelIndex = ChannelValueKvp.Key - 1;

				if (Signal->ChannelData.IsValidIndex(ChannelIndex))
				{
					Signal->ChannelData[ChannelIndex] = ChannelValueKvp.Value;
				}
			}

			Signal->ExternUniverseID = RemoteUniverse;
			Signal->Timestamp = FPlatformTime::Seconds();

			// Send via the protocol's sender
			if (bNeedsSendDMX)
			{
				DMXSender->SendDMXSignal(Signal);
			}

			// Loopback to Listeners
			for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
			{
				RawListener->EnqueueSignal(this, Signal);
			}
		}
	}
}

bool FDMXOutputPort::Register()
{
	if (Protocol.IsValid() && IsValidPortSlow() && CommunicationDeterminator.IsSendDMXEnabled() && !FDMXPortManager::Get().AreProtocolsSuspended())
	{
		DMXSender = Protocol->RegisterOutputPort(SharedThis(this));

		if (DMXSender.IsValid())
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

		DMXSender = nullptr;
	}

	CommunicationDeterminator.SetHasValidSender(false);
}

void FDMXOutputPort::ClearBuffers()
{
	if (DMXSender)
	{
		DMXSender->ClearBuffer();
	}

	for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
	{
		RawListener->ClearBuffer();
	}

	ExternUniverseToLatestSignalMap.Reset();
}

bool FDMXOutputPort::IsLoopbackToEngine() const
{
	return CommunicationDeterminator.NeedsLoopbackToEngine();
}

bool FDMXOutputPort::GameThreadGetDMXSignal(int32 LocalUniverseID, FDMXSignalSharedPtr& OutDMXSignal, bool bEvenIfNotLoopbackToEngine)
{
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();
	if (bNeedsLoopbackToEngine || bEvenIfNotLoopbackToEngine)
	{
		int32 ExternUniverseID = ConvertLocalToExternUniverseID(LocalUniverseID);

		const FDMXSignalSharedPtr* SignalPtr = ExternUniverseToLatestSignalMap.Find(ExternUniverseID);
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
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();
	if (bNeedsLoopbackToEngine || bEvenIfNotLoopbackToEngine)
	{
		const FDMXSignalSharedPtr* SignalPtr = ExternUniverseToLatestSignalMap.Find(RemoteUniverseID);
		if (SignalPtr)
		{
			OutDMXSignal = *SignalPtr;
			return true;
		}
	}

	return false;
}

void FDMXOutputPort::OnSetSendDMXEnabled(bool bEnabled)
{
	CommunicationDeterminator.SetSendEnabled(bEnabled);

	UpdateFromConfig(*FindOutputPortConfigChecked());
}

void FDMXOutputPort::OnSetReceiveDMXEnabled(bool bEnabled)
{
	CommunicationDeterminator.SetReceiveEnabled(bEnabled);

	UpdateFromConfig(*FindOutputPortConfigChecked());
}

FDMXOutputPortConfig* FDMXOutputPort::FindOutputPortConfigChecked() const
{
	UDMXProtocolSettings* ProjectSettings = GetMutableDefault<UDMXProtocolSettings>();

	for (FDMXOutputPortConfig& OutputPortConfig : ProjectSettings->OutputPortConfigs)
	{
		if (OutputPortConfig.GetPortGuid() == PortGuid)
		{
			return &OutputPortConfig;
		}
	}

	// Check failed, no config found
	checkNoEntry();

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
