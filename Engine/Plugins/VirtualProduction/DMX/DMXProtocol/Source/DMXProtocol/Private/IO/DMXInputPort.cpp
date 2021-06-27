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


FDMXInputPortSharedRef FDMXInputPort::CreateFromConfig(FDMXInputPortConfig& InputPortConfig)
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

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Created input port %s"), *NewInputPort->PortName);

	return NewInputPort;
}

FDMXInputPort::~FDMXInputPort()
{
	// All Inputs need to be explicitly removed before destruction 
	check(RawListeners.Num() == 0);
	check(LocalUniverseToListenerGroupMap.Num() == 0);

	// Port needs be unregistered before destruction
	check(!bRegistered);

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Destroyed input port %s"), *PortName);
}

bool FDMXInputPort::CheckPriority(const int32 InPriority)
{
	if (InPriority > HighestReceivedPriority)
	{
		HighestReceivedPriority = InPriority;
	}
	
	if (InPriority < LowestReceivedPriority)
	{
		LowestReceivedPriority = InPriority;
	}
	
	switch (PriorityStrategy)
	{
	case(EDMXPortPriorityStrategy::None):
		return true;
	case(EDMXPortPriorityStrategy::HigherThan):
		return InPriority > Priority;
	case(EDMXPortPriorityStrategy::Equal):
		return InPriority == Priority;
	case(EDMXPortPriorityStrategy::LowerThan):
		return InPriority < Priority;
	case(EDMXPortPriorityStrategy::Highest):
		return InPriority >= HighestReceivedPriority;
	case(EDMXPortPriorityStrategy::Lowest):
		return InPriority <= LowestReceivedPriority;
	default:
		break;
	}

	return false;
}

void FDMXInputPort::UpdateFromConfig(FDMXInputPortConfig& InputPortConfig)
{
	// Need a valid config for the port
	InputPortConfig.MakeValid();

	// Find if the port needs update its registration with the protocol
	const bool bNeedsUpdateRegistration = [this, &InputPortConfig]()
	{
		if (IsRegistered() != bReceiveDMXEnabled)
		{
			return true;
		}

		if (IsRegistered() && FDMXPortManager::Get().AreProtocolsSuspended())
		{
			return true;
		}

		FName ProtocolName = Protocol.IsValid() ? Protocol->GetProtocolName() : NAME_None;

		if (ProtocolName == InputPortConfig.GetProtocolName() &&
			DeviceAddress == InputPortConfig.GetDeviceAddress() &&
			CommunicationType == InputPortConfig.GetCommunicationType())
		{
			return false;
		}

		return true;
	}();

	// Unregister the port if required before the new protocol is set
	if (bNeedsUpdateRegistration)
	{
		Unregister();
	}

	Protocol = IDMXProtocol::Get(InputPortConfig.GetProtocolName());

	// Copy properties from the config into the base class
	PortGuid = InputPortConfig.GetPortGuid();
	CommunicationType = InputPortConfig.GetCommunicationType();
	ExternUniverseStart = InputPortConfig.GetExternUniverseStart();
	DeviceAddress = InputPortConfig.GetDeviceAddress();
	LocalUniverseStart = InputPortConfig.GetLocalUniverseStart();
	NumUniverses = InputPortConfig.GetNumUniverses();
	PortName = InputPortConfig.GetPortName();
	PriorityStrategy = InputPortConfig.GetPortPriorityStrategy();
	Priority = InputPortConfig.GetPriority();

	// Re-register the port if required
	if (bNeedsUpdateRegistration)
	{
		Register();
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

void FDMXInputPort::AddRawListener(TSharedRef<FDMXRawListener> InRawListener)
{
	check(!RawListeners.Contains(InRawListener));

	// Inputs need to run in the game thread
	check(IsInGameThread());

	RawListeners.Add(InRawListener);
}

void FDMXInputPort::RemoveRawListener(TSharedRef<FDMXRawListener> InRawListenerToRemove)
{
	RawListeners.Remove(InRawListenerToRemove);
}

bool FDMXInputPort::Register()
{
	if (Protocol.IsValid() && IsValidPortSlow() && bReceiveDMXEnabled && !FDMXPortManager::Get().AreProtocolsSuspended())
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
		if (Protocol.IsValid())
		{
			Protocol->UnregisterInputPort(SharedThis(this));
		}

		bRegistered = false;
	}
}

void FDMXInputPort::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_DMXInputPortTick);

	// Tick universe Inputs with latest signals on tick
	FDMXSignalSharedPtr Signal;

	if (bUseDefaultInputQueue)
	{
		while (DefaultInputQueue.Dequeue(Signal))
		{
			// No need to filter extern universe, we already did that when enqueing
			ExternUniverseToLatestSignalMap.FindOrAdd(Signal->ExternUniverseID) = Signal;
		}
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
	// Needs be called from the game thread, to maintain thread safety with ExternUniverseToLatestSignalMap
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
	{
		RawListener->ClearBuffer();
	}

	DefaultInputQueue.Empty();
	ExternUniverseToLatestSignalMap.Reset();
}

void FDMXInputPort::SingleProducerInputDMXSignal(const FDMXSignalSharedRef& DMXSignal)
{
	if (IsReceiveDMXEnabled())
	{
		int32 ExternUniverseID = DMXSignal->ExternUniverseID;
		if (IsExternUniverseInPortRange(ExternUniverseID))
		{
			for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
			{
				RawListener->EnqueueSignal(this, DMXSignal);
			}

			if (bUseDefaultInputQueue)
			{
				DefaultInputQueue.Enqueue(DMXSignal);
			}
		}
	}
}

bool FDMXInputPort::GameThreadGetDMXSignal(int32 LocalUniverseID, FDMXSignalSharedPtr& OutDMXSignal)
{
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	int32 ExternUniverseID = ConvertLocalToExternUniverseID(LocalUniverseID);

	const FDMXSignalSharedPtr* SignalPtr = ExternUniverseToLatestSignalMap.Find(ExternUniverseID);
	if (SignalPtr)
	{
		OutDMXSignal = *SignalPtr;
		return true;
	}

	return false;
}

const TMap<int32, FDMXSignalSharedPtr>& FDMXInputPort::GameThreadGetAllDMXSignals() const
{
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	return ExternUniverseToLatestSignalMap;
}

bool FDMXInputPort::GameThreadGetDMXSignalFromRemoteUniverse(FDMXSignalSharedPtr& OutDMXSignal, int32 RemoteUniverseID, bool bEvenIfNotLoopbackToEngine)
{
	// DEPRECATED 4.27
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	const FDMXSignalSharedPtr* SignalPtr = ExternUniverseToLatestSignalMap.Find(RemoteUniverseID);
	if (SignalPtr)
	{
		OutDMXSignal = *SignalPtr;
		return true;
	}

	return false;
}

void FDMXInputPort::GameThreadInjectDMXSignal(const FDMXSignalSharedRef& DMXSignal)
{
	ensureMsgf(!bUseDefaultInputQueue || !bReceiveDMXEnabled || FDMXPortManager::Get().AreProtocolsSuspended(), TEXT("Potential conflicts between injected and received signals, please revise the implementation."));

	int32 ExternUniverseID = DMXSignal->ExternUniverseID;
	if (IsExternUniverseInPortRange(ExternUniverseID))
	{
		ExternUniverseToLatestSignalMap.FindOrAdd(ExternUniverseID, DMXSignal) = DMXSignal;
	}
}

void FDMXInputPort::SetUseDefaultQueue(bool bUse)
{
	bUseDefaultInputQueue = bUse;
}

void FDMXInputPort::OnSetReceiveDMXEnabled(bool bEnabled)
{
	bReceiveDMXEnabled = bEnabled;

	UpdateFromConfig(*FindInputPortConfigChecked());
}

FDMXInputPortConfig* FDMXInputPort::FindInputPortConfigChecked() const
{
	UDMXProtocolSettings* ProjectSettings = GetMutableDefault<UDMXProtocolSettings>();

	for (FDMXInputPortConfig& InputPortConfig : ProjectSettings->InputPortConfigs)
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
