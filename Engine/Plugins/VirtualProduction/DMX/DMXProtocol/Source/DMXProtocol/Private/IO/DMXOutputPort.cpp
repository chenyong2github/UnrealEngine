// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXOutputPort.h"

#include "DMXProtocolSettings.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXSender.h"
#include "IO/DMXOutputPortConfig.h"
#include "IO/DMXRawListener.h"


#define LOCTEXT_NAMESPACE "DMXOutputPort"

FDMXOutputPort::FDMXOutputPort()
	: DMXSender(nullptr)
{}

FDMXOutputPort::~FDMXOutputPort()
{	
	// All Inputs need to be explicitly removed before destruction 
	check(RawListeners.Num() == 0);
	check(LocalUniverseToListenerGroupMap.Num() == 0);
	
	// Port needs be unregistered before destruction
	check(!DMXSender);
}

void FDMXOutputPort::Initialize(const FGuid& InPortGuid)
{
	UDMXProtocolSettings* Settings = GetMutableDefault<UDMXProtocolSettings>();
	check(Settings);

	// Bind to send dmx changes
	bSendDMXEnabled = Settings->IsSendDMXEnabled();
	Settings->OnSetSendDMXEnabled.AddThreadSafeSP(this, &FDMXOutputPort::OnSetSendDMXEnabled);

	// Bind to receive dmx changes
	bReceiveDMXEnabled = Settings->IsReceiveDMXEnabled();
	Settings->OnSetReceiveDMXEnabled.AddThreadSafeSP(this, &FDMXOutputPort::OnSetReceiveDMXEnabled);
		
	PortGuid = InPortGuid;
	
	UpdateFromConfig();
}

void FDMXOutputPort::UpdateFromConfig()
{
	const FDMXOutputPortConfig& OutputPortConfig = *FindOutputPortConfigChecked();

	// Find if the port needs update its registration with the protocol
	const bool bNeedsUpdateRegistration = [this, &OutputPortConfig]()
	{
		if (IsRegistered())
		{
			FName ProtocolName = Protocol.IsValid() ? Protocol->GetProtocolName() : NAME_None;
			if (ProtocolName == OutputPortConfig.ProtocolName ||
				Address == OutputPortConfig.Address ||
				CommunicationType == OutputPortConfig.CommunicationType)
			{
				return false;
			}
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

	Protocol = IDMXProtocol::Get(OutputPortConfig.ProtocolName);

	// Copy properties from the config
	CommunicationType = OutputPortConfig.CommunicationType;
	ExternUniverseStart = OutputPortConfig.ExternUniverseStart;
	Address = OutputPortConfig.Address;
	bLoopbackToEngine = OutputPortConfig.bLoopbackToEngine;
	LocalUniverseStart = OutputPortConfig.LocalUniverseStart;
	NumUniverses = OutputPortConfig.NumUniverses;
	PortName = OutputPortConfig.PortName;
	Priority = OutputPortConfig.Priority;

	// Re-register the port if required
	if (bNeedsUpdateRegistration)
	{
		if (IsValidPortSlow())
		{
			Register();
		}
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

void FDMXOutputPort::AddRawInput(TSharedRef<FDMXRawListener> RawInput)
{
	check(!RawListeners.Contains(RawInput));

	// Inputs need to run in the game thread
	check(IsInGameThread());

	RawListeners.Add(RawInput);
}

void FDMXOutputPort::RemoveRawInput(TSharedRef<FDMXRawListener> RawInput)
{
	RawListeners.Remove(RawInput);
}

void FDMXOutputPort::SendDMX(int32 LocalUniverseID, const TMap<int32, uint8>& ChannelToValueMap)
{
	if (DMXSender.IsValid() &&
		IsLocalUniverseInPortRange(LocalUniverseID))
	{
		int32 ExternUniverseID = ConvertLocalToExternUniverseID(LocalUniverseID);

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

			if (Signal->ChannelData.IsValidIndex(ChannelIndex))
			{
				Signal->ChannelData[ChannelIndex] = ChannelValueKvp.Value;
			}
		}

		Signal->ExternUniverseID = ExternUniverseID;
		Signal->Timestamp = FPlatformTime::Seconds();

		// Send via the protocol's sender
		if(IsSendDMXEnabled())
		{
			DMXSender->SendDMXSignal(Signal);
		}

		// Loopback to Listeners
		if (bLoopbackToEngine)
		{
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
	if (DMXSender.IsValid() &&
		IsExternUniverseInPortRange(RemoteUniverse))
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
		if (IsSendDMXEnabled())
		{
			DMXSender->SendDMXSignal(Signal);
		}

		// Loopback to Listeners
		if (bLoopbackToEngine)
		{
			for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
			{
				RawListener->EnqueueSignal(this, Signal);
			}
		}
	}
}

bool FDMXOutputPort::Register()
{
	if (Protocol.IsValid())
	{
		DMXSender = Protocol->RegisterOutputPort(SharedThis(this));

		if (DMXSender)
		{
			return true;
		}
	}

	return false;
}

void FDMXOutputPort::Unregister()
{
	if (IsRegistered())
	{
		check(Protocol.IsValid());

		Protocol->UnregisterOutputPort(SharedThis(this));

		DMXSender = nullptr;
	}
}

void FDMXOutputPort::ClearBuffers()
{
	if (DMXSender)
	{
		DMXSender->ClearBuffer();
	}

	for (const TSharedRef<FDMXRawListener>& RawInput : RawListeners)
	{
		RawInput->ClearBuffer();
	}
}

bool FDMXOutputPort::GameThreadGetDMXSignal(int32 LocalUniverseID, FDMXSignalSharedPtr& OutDMXSignal, bool bEvenIfNotLoopbackToEngine)
{
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	if (bLoopbackToEngine || bEvenIfNotLoopbackToEngine)
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

	if (bLoopbackToEngine || bEvenIfNotLoopbackToEngine)
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
	bSendDMXEnabled = bEnabled;
}

void FDMXOutputPort::OnSetReceiveDMXEnabled(bool bEnabled)
{
	bReceiveDMXEnabled = bEnabled;
}

const FDMXOutputPortConfig* FDMXOutputPort::FindOutputPortConfigChecked() const
{
	const UDMXProtocolSettings* ProjectSettings = GetDefault<UDMXProtocolSettings>();

	for (const FDMXOutputPortConfig& OutputPortConfig : ProjectSettings->OutputPortConfigs)
	{
		if (OutputPortConfig.GetPortGuid() == PortGuid)
		{
			return &OutputPortConfig;
		}
	}

	checkNoEntry();

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
