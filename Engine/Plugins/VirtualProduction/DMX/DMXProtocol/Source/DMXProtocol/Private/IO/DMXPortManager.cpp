// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXPortManager.h"

#include "DMXProtocolSettings.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXInputPortConfig.h"
#include "IO/DMXOutputPortConfig.h"

#include "Templates/UniquePtr.h"
#include "UObject/UnrealType.h"


TUniquePtr<FDMXPortManager> FDMXPortManager::CurrentManager;

FDMXPortManager::~FDMXPortManager()
{
	// If this check is hit, the manager never was shut down
	check(!CurrentManager.IsValid());
}

FDMXPortManager& FDMXPortManager::Get()
{
#if UE_BUILD_DEBUG
	check(CurrentManager.IsValid());
#endif // UE_BUILD_DEBUG

	return *CurrentManager;
}

FDMXPortSharedRef FDMXPortManager::FindPortByGuidChecked(const FGuid& PortGuid) const
{
	const FDMXInputPortSharedRef* InputPortPtr =
		InputPorts.FindByPredicate([&PortGuid](const FDMXInputPortSharedRef& InputPort) {
		return InputPort->GetPortGuid() == PortGuid;
			});

	if (InputPortPtr)
	{
		return (*InputPortPtr);
	}

	const FDMXOutputPortSharedRef* OutputPortPtr =
		OutputPorts.FindByPredicate([&PortGuid](const FDMXOutputPortSharedRef& OutputPort) {
		return OutputPort->GetPortGuid() == PortGuid;
			});

	if (OutputPortPtr)
	{
		return (*OutputPortPtr);
	}

	// Check failed
	checkNoEntry();
	FDMXPortSharedPtr InvalidPtr;
	return InvalidPtr.ToSharedRef();
}

void FDMXPortManager::StartupManager()
{
	UE_LOG(LogDMXProtocol, Log, TEXT("Startup DMXPortManager"));

	check(!CurrentManager.IsValid());
	CurrentManager = MakeUnique<FDMXPortManager>();
	CurrentManager->StartupManagerInternal();
}

void FDMXPortManager::ShutdownManager()
{
	UE_LOG(LogDMXProtocol, Log, TEXT("Shutdown DMXPortManager"));

	check(CurrentManager.IsValid());
	CurrentManager->ShutdownManagerInternal();
	
	CurrentManager.Reset();
}

#if WITH_EDITOR
void FDMXPortManager::NotifyPortConfigChanged(const FGuid& PortGuid)
{
	check(PortGuid.IsValid());

	const FDMXInputPortSharedRef* InputPortPtr =
		InputPorts.FindByPredicate([&PortGuid](const FDMXInputPortSharedRef& InputPort) {
			return InputPort->GetPortGuid() == PortGuid;
		});

	if (InputPortPtr)
	{
		(*InputPortPtr)->UpdateFromConfig();
		return;
	}

	const FDMXOutputPortSharedRef* OutputPortPtr =
		OutputPorts.FindByPredicate([&PortGuid](const FDMXOutputPortSharedRef& InputPort) {
			return InputPort->GetPortGuid() == PortGuid;
		});

	if (OutputPortPtr)
	{
		(*OutputPortPtr)->UpdateFromConfig();
		return;
	}

	EditorEditedPort.Broadcast(PortGuid);
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void FDMXPortManager::NotifyPortConfigArraysChanged()
{
	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
	check(ProtocolSettings);

	// Add new ports
	for (FDMXInputPortConfig& InputPortConfig : ProtocolSettings->InputPortConfigs)
	{
		if (!InputPortConfig.IsInitialized())
		{
			SetupInputPort(InputPortConfig);
		}
	}

	for (FDMXOutputPortConfig& OutputPortConfig : ProtocolSettings->OutputPortConfigs)
	{
		if (!OutputPortConfig.IsInitialized())
		{
			SetupOutputPort(OutputPortConfig);
		}
	}

	// Remove deleted ports
	InputPorts.RemoveAll([ProtocolSettings](const TSharedPtr<FDMXInputPort, ESPMode::ThreadSafe>& InputPort) {

		const bool bPortExists = ProtocolSettings->InputPortConfigs.ContainsByPredicate([&InputPort](const FDMXInputPortConfig& PortConfig) {
			return PortConfig.GetPortGuid() == InputPort->GetPortGuid();
		});
	
		return bPortExists;
	});

	OutputPorts.RemoveAll([ProtocolSettings](const TSharedPtr<FDMXOutputPort, ESPMode::ThreadSafe>& OutputPort) {

		const bool bPortExists = ProtocolSettings->OutputPortConfigs.ContainsByPredicate([&OutputPort](const FDMXOutputPortConfig& PortConfig) {
			return PortConfig.GetPortGuid() == OutputPort->GetPortGuid();
		});
	
		return bPortExists;
	});

	EditorChangedPorts.Broadcast();
}
#endif // WITH_EDITOR

void FDMXPortManager::StartupManagerInternal()
{
	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
	check(ProtocolSettings);

	for (FDMXInputPortConfig& InputPortConfig : ProtocolSettings->InputPortConfigs)
	{
		SetupInputPort(InputPortConfig);
	}

	for (FDMXOutputPortConfig& OutputPortConfig : ProtocolSettings->OutputPortConfigs)
	{
		SetupOutputPort(OutputPortConfig);
	}
}

void FDMXPortManager::ShutdownManagerInternal()
{
	InputPorts.Reset();
	OutputPorts.Reset();
}

void FDMXPortManager::SetupInputPort(FDMXInputPortConfig& MutablePortConfig)
{
	FDMXInputPortSharedRef InputPort = MakeShared<FDMXInputPort, ESPMode::ThreadSafe>();
	InputPorts.Add(InputPort);

	// Init the config and acquire its Port Guid
	FGuid PortGuid = MutablePortConfig.Initialize();

	// Init the port with the Port Config's Port Guid
	InputPort->Initialize(PortGuid);
}

void FDMXPortManager::SetupOutputPort(FDMXOutputPortConfig& MutablePortConfig)
{
	FDMXOutputPortSharedRef OutputPort = MakeShared<FDMXOutputPort, ESPMode::ThreadSafe>();
	OutputPorts.Add(OutputPort);

	// Init the config and acquire its Port Guid
	FGuid PortGuid = MutablePortConfig.Initialize();

	// Init the port with the Port Config's Port Guid
	OutputPort->Initialize(PortGuid);
}
