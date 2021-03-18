// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXInputPort.h"

#include "DMXProtocolSettings.h"
#include "DMXStats.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXInputPortConfig.h"
#include "IO/DMXRawListener.h"

DECLARE_CYCLE_STAT(TEXT("Input Port Tick"), STAT_DMXInputPortTick, STATGROUP_DMX);


#define LOCTEXT_NAMESPACE "DMXInputPort"

FDMXInputPort::~FDMXInputPort()
{
	// All Inputs need to be explicitly removed before destruction 
	check(RawListeners.Num() == 0);
	check(LocalUniverseToListenerGroupMap.Num() == 0);

	// Port needs be unregistered before destruction
	check(!bRegistered);
}

void FDMXInputPort::Initialize(const FGuid& InPortGuid)
{
	UDMXProtocolSettings* Settings = GetMutableDefault<UDMXProtocolSettings>();
	check(Settings);

	// Bind to receive dmx changes
	bReceiveDMXEnabled = Settings->IsReceiveDMXEnabled();
	Settings->OnSetReceiveDMXEnabled.AddThreadSafeSP(this, &FDMXInputPort::OnSetReceiveDMXEnabled);

	PortGuid = InPortGuid;

	UpdateFromConfig();
}

void FDMXInputPort::UpdateFromConfig()
{
	const FDMXInputPortConfig& InputPortConfig = *FindInputPortConfigChecked();

	// Find if the port needs update its registration with the protocol
	const bool bNeedsUpdateRegistration = [this, &InputPortConfig]()
	{
		if (IsRegistered())
		{
			FName ProtocolName = Protocol.IsValid() ? Protocol->GetProtocolName() : NAME_None;
			if (ProtocolName == InputPortConfig.ProtocolName ||
				Address == InputPortConfig.Address ||
				CommunicationType == InputPortConfig.CommunicationType)
			{
				return false;
			}
		}

		return true;
	}();

	Protocol = IDMXProtocol::Get(InputPortConfig.ProtocolName);

	// Copy properties from the config into the base class
	CommunicationType = InputPortConfig.CommunicationType;
	ExternUniverseStart = InputPortConfig.ExternUniverseStart;
	Address = InputPortConfig.Address;
	LocalUniverseStart = InputPortConfig.LocalUniverseStart;
	NumUniverses = InputPortConfig.NumUniverses;
	PortName = InputPortConfig.PortName;

	// Re-register the port if required
	if (bNeedsUpdateRegistration)
	{
		if (IsRegistered())
		{
			Unregister();
		}

		if (IsValidPortSlow())
		{
			Register();
		}
	}

	OnPortUpdated.Broadcast();
}

const FGuid& FDMXInputPort::GetPortGuid() const
{
	check(PortGuid.IsValid());
	return PortGuid;
}

bool FDMXInputPort::IsRegistered() const
{
	return bRegistered;
}

void FDMXInputPort::AddRawInput(TSharedRef<FDMXRawListener> RawInput)
{
	check(!RawListeners.Contains(RawInput));

	// Inputs need to run in the game thread
	check(IsInGameThread());

	RawListeners.Add(RawInput);
}

void FDMXInputPort::RemoveRawInput(TSharedRef<FDMXRawListener> RawInput)
{
	RawListeners.Remove(RawInput);
}

bool FDMXInputPort::Register()
{
	if (Protocol.IsValid())
	{
		bRegistered = Protocol->RegisterInputPort(SharedThis(this));

		return bRegistered;
	}

	return false;
}

void FDMXInputPort::Unregister()
{
	if (bRegistered)
	{
		check(Protocol.IsValid());

		Protocol->UnregisterInputPort(SharedThis(this));
	
		bRegistered = false;
	}
}

void FDMXInputPort::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_DMXInputPortTick);

	// Tick universe Inputs with latest signals on tick
	FDMXSignalSharedPtr Signal;
	while (TickedBuffer.Dequeue(Signal))
	{
		// No need to fliter extern universe, we already did that when enqueing
		UniverseToLatestSignalMap.FindOrAdd(Signal->ExternUniverseID) = Signal;
	}
}

ETickableTickType FDMXInputPort::GetTickableTickType() const
{
	return ETickableTickType::Always;
}

TStatId FDMXInputPort::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FDMXInputPort, STATGROUP_Tickables);
}

void FDMXInputPort::ClearBuffers()
{
#if UE_BUILD_DEBUG
	// Needs be called from the game thread, to maintain thread safety with UniverseToLatestSignalMap
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG
	
	for (const TSharedRef<FDMXRawListener>& RawInput : RawListeners)
	{
		RawInput->ClearBuffer();
	}

	TickedBuffer.Empty();
	UniverseToLatestSignalMap.Reset();
}

void FDMXInputPort::SingleProducerInputDMXSignal(const FDMXSignalSharedRef& DMXSignal)
{
	if (IsReceiveDMXEnabled())
	{
		int32 ExternUniverseID = DMXSignal->ExternUniverseID;
		if (IsExternUniverseInPortRange(ExternUniverseID))
		{
			int32 LocalUniverseID = ConvertExternToLocalUniverseID(ExternUniverseID);

			for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
			{
				RawListener->EnqueueSignal(this, DMXSignal);
			}

			TickedBuffer.Enqueue(DMXSignal);
		}
	}
}

bool FDMXInputPort::GameThreadGetDMXSignal(int32 LocalUniverseID, FDMXSignalSharedPtr& OutDMXSignal)
{
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	int32 ExternUniverseID = ConvertLocalToExternUniverseID(LocalUniverseID);

	const FDMXSignalSharedPtr* SignalPtr = UniverseToLatestSignalMap.Find(ExternUniverseID);
	if (SignalPtr)
	{
		OutDMXSignal = *SignalPtr;
		return true;
	}
	
	return false;
}

bool FDMXInputPort::GameThreadGetDMXSignalFromRemoteUniverse(FDMXSignalSharedPtr& OutDMXSignal, int32 RemoteUniverseID, bool bEvenIfNotLoopbackToEngine)
{
	// DEPRECATED 4.27
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	const FDMXSignalSharedPtr* SignalPtr = UniverseToLatestSignalMap.Find(RemoteUniverseID);
	if (SignalPtr)
	{
		OutDMXSignal = *SignalPtr;
		return true;
	}

	return false;
}

void FDMXInputPort::OnSetReceiveDMXEnabled(bool bEnabled)
{
	bReceiveDMXEnabled = bEnabled;
}

const FDMXInputPortConfig* FDMXInputPort::FindInputPortConfigChecked() const
{
	const UDMXProtocolSettings* ProjectSettings = GetDefault<UDMXProtocolSettings>();

	for (const FDMXInputPortConfig& InputPortConfig : ProjectSettings->InputPortConfigs)
	{
		if (InputPortConfig.GetPortGuid() == PortGuid)
		{
			return &InputPortConfig;
		}
	}

	// Check failed, no config found
	checkNoEntry();

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
