// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXInputPort.h"

#include "DMXProtocolSettings.h"
#include "DMXStats.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXInputPortConfig.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXRawListener.h"


DECLARE_CYCLE_STAT(TEXT("Input Port Tick"), STAT_DMXInputPortTick, STATGROUP_DMX);


#define LOCTEXT_NAMESPACE "DMXInputPort"

FDMXInputPortSharedRef FDMXInputPort::Create()
{
	FDMXInputPortSharedRef NewInputPort = MakeShared<FDMXInputPort, ESPMode::ThreadSafe>();

	NewInputPort->PortGuid = FGuid::NewGuid();

	UDMXProtocolSettings* Settings = GetMutableDefault<UDMXProtocolSettings>();
	check(Settings);

	NewInputPort->bReceiveDMXEnabled = Settings->IsReceiveDMXEnabled();

	// Bind to receive dmx changes
	Settings->OnSetReceiveDMXEnabled.AddThreadSafeSP(NewInputPort, &FDMXInputPort::OnSetReceiveDMXEnabled);

	return NewInputPort;
}

FDMXInputPortSharedRef FDMXInputPort::CreateFromConfig(const FDMXInputPortConfig& InputPortConfig)
{
	// Port Configs are expected to have a valid guid always
	check(InputPortConfig.GetPortGuid().IsValid());

	FDMXInputPortSharedRef NewInputPort = MakeShared<FDMXInputPort, ESPMode::ThreadSafe>();

	NewInputPort->PortGuid = InputPortConfig.GetPortGuid();

	UDMXProtocolSettings* Settings = GetMutableDefault<UDMXProtocolSettings>();
	check(Settings);

	NewInputPort->bReceiveDMXEnabled = Settings->IsReceiveDMXEnabled();

	// Bind to receive dmx changes
	Settings->OnSetReceiveDMXEnabled.AddThreadSafeSP(NewInputPort, &FDMXInputPort::OnSetReceiveDMXEnabled);

	NewInputPort->UpdateFromConfig(InputPortConfig);

	return NewInputPort;
}

FDMXInputPort::~FDMXInputPort()
{
	// All Inputs need to be explicitly removed before destruction 
	check(RawListeners.Num() == 0);
	check(LocalUniverseToListenerGroupMap.Num() == 0);

	// Port needs be unregistered before destruction
	check(!bRegistered);
}

void FDMXInputPort::UpdateFromConfig(const FDMXInputPortConfig& InputPortConfig)
{
	// Find if the port needs update its registration with the protocol
	const bool bNeedsUpdateRegistration = [this, &InputPortConfig]()
	{
		if (!IsRegistered())
		{
			return true;
		}

		FName ProtocolName = Protocol.IsValid() ? Protocol->GetProtocolName() : NAME_None;

		if (ProtocolName == InputPortConfig.ProtocolName &&
			DeviceAddress == InputPortConfig.DeviceAddress &&
			CommunicationType == InputPortConfig.CommunicationType)
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

	Protocol = IDMXProtocol::Get(InputPortConfig.ProtocolName);

	// Copy properties from the config into the base class
	CommunicationType = InputPortConfig.CommunicationType;
	ExternUniverseStart = InputPortConfig.ExternUniverseStart;
	DeviceAddress = InputPortConfig.DeviceAddress;
	LocalUniverseStart = InputPortConfig.LocalUniverseStart;
	NumUniverses = InputPortConfig.NumUniverses;
	PortName = InputPortConfig.PortName;

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
		// No need to filter extern universe, we already did that when enqueing
		UniverseToLatestSignalMap.FindOrAdd(Signal->ExternUniverseID) = Signal;
		FDMXPortManager::Get().OnPortInputDequeued.Broadcast(FDMXInputPort::SharedThis(this), Signal.ToSharedRef());
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
