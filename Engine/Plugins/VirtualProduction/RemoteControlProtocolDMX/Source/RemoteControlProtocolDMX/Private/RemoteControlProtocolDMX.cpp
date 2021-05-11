// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolDMX.h"

#include "DMXProtocolSettings.h"
#include "DMXProtocolTypes.h"
#include "RemoteControlLogger.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXPortManager.h"
#include "Library/DMXEntityFixtureType.h"

#define LOCTEXT_NAMESPACE "RemoteControlProtocolDMX"

const FName FRemoteControlProtocolDMX::ProtocolName = TEXT("DMX");

FRemoteControlDMXProtocolEntity::~FRemoteControlDMXProtocolEntity()
{
	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
	if (PortsChangedHandle.IsValid())
	{
		FDMXPortManager::Get().OnPortsChanged.Remove(PortsChangedHandle);
	}
}

void FRemoteControlDMXProtocolEntity::Initialize()
{
	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();

	// Add Delegates
	PortsChangedHandle = FDMXPortManager::Get().OnPortsChanged.AddRaw(this, &FRemoteControlDMXProtocolEntity::UpdateInputPort);

    // Assign InputPortReference
	UpdateInputPort();
}

void FRemoteControlDMXProtocolEntity::UpdateInputPort()
{
	// Reset port Id;
	InputPortId = FGuid();
	
	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
	
	const TArray<FDMXInputPortSharedRef>& InputPorts = FDMXPortManager::Get().GetInputPorts();
	for (const FDMXInputPortConfig& PortConfig : ProtocolSettings->InputPortConfigs)
	{
		const FGuid& InputPortGuid = PortConfig.GetPortGuid();

		const FDMXInputPortSharedRef* InputPortPtr = InputPorts.FindByPredicate([&InputPortGuid](const FDMXInputPortSharedRef& InputPort) {
            return InputPort->GetPortGuid() == InputPortGuid;
            });

		if (InputPortPtr == nullptr)
		{
			break;
		}

		const FDMXInputPortSharedRef& InputPort = *InputPortPtr;
		if (InputPort->IsLocalUniverseInPortRange(Universe))
		{
			InputPortId = InputPortGuid;
		}
	}
}

void FRemoteControlProtocolDMX::Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr)
{
	if (!ensure(InRemoteControlProtocolEntityPtr.IsValid()))
	{
		return;
	}

	FRemoteControlDMXProtocolEntity* DMXProtocolEntity = InRemoteControlProtocolEntityPtr->CastChecked<FRemoteControlDMXProtocolEntity>();

	FRemoteControlProtocolEntityWeakPtr* ExistingProtocolBindings = ProtocolsBindings.FindByPredicate([DMXProtocolEntity](const FRemoteControlProtocolEntityWeakPtr& InProtocolEntity)
		{
			if (const FRemoteControlProtocolEntityPtr& ProtocolEntity = InProtocolEntity.Pin())
			{
				const FRemoteControlDMXProtocolEntity* ComparedDMXProtocolEntity = ProtocolEntity->CastChecked<FRemoteControlDMXProtocolEntity>();

				if (ComparedDMXProtocolEntity->Universe == DMXProtocolEntity->Universe &&
					ComparedDMXProtocolEntity->ExtraSetting.StartingChannel == DMXProtocolEntity->ExtraSetting.StartingChannel &&
					ComparedDMXProtocolEntity->GetPropertyId() == DMXProtocolEntity->GetPropertyId())
				{
					return true;
				}
			}
			
			return false;
		});


	if (ExistingProtocolBindings == nullptr)
	{
		ProtocolsBindings.Emplace(InRemoteControlProtocolEntityPtr);
	}

	DMXProtocolEntity->Initialize();
}

void FRemoteControlProtocolDMX::Unbind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr)
{
	if (!ensure(InRemoteControlProtocolEntityPtr.IsValid()))
	{
		return;
	}

	const FRemoteControlDMXProtocolEntity* DMXProtocolEntity = InRemoteControlProtocolEntityPtr->CastChecked<FRemoteControlDMXProtocolEntity>();
	ProtocolsBindings.RemoveAllSwap(CreateProtocolComparator(DMXProtocolEntity->GetPropertyId()));
}

void FRemoteControlProtocolDMX::OnEndFrame()
{
	if (!ProtocolsBindings.Num())
	{
		return;
	}

	const TArray<FDMXInputPortSharedRef>& InputPorts = FDMXPortManager::Get().GetInputPorts();
	for (const FRemoteControlProtocolEntityWeakPtr& ProtocolEntityWeakPtr : ProtocolsBindings)
	{
		if (const FRemoteControlProtocolEntityPtr& ProtocolEntity = ProtocolEntityWeakPtr.Pin())
		{
			FRemoteControlDMXProtocolEntity* DMXProtocolEntity = ProtocolEntity->CastChecked<FRemoteControlDMXProtocolEntity>();

			const FGuid& PortId =  DMXProtocolEntity->InputPortId;				

			const FDMXInputPortSharedRef* InputPortPtr = InputPorts.FindByPredicate([&PortId](const FDMXInputPortSharedRef& InputPort) {
                return InputPort->GetPortGuid() == PortId;
                });

			if (InputPortPtr == nullptr)
			{
				break;
			}

			const FDMXInputPortSharedRef& InputPort = *InputPortPtr;

			// Get universe DMX signal
			InputPort->GameThreadGetDMXSignal(DMXProtocolEntity->Universe, DMXProtocolEntity->LastSignalPtr);
			if (DMXProtocolEntity->LastSignalPtr.IsValid())
			{
				const FDMXSignalSharedPtr& LastSignalPtr = DMXProtocolEntity->LastSignalPtr;
				const int32 DMXOffset = DMXProtocolEntity->ExtraSetting.StartingChannel - 1;
				check(DMXOffset >= 0 && DMXOffset < DMX_UNIVERSE_SIZE);
				
				ProcessAndApplyProtocolValue(LastSignalPtr, DMXOffset, ProtocolEntity);
			}
		}
	}

	FRemoteControlProtocol::OnEndFrame();
}

void FRemoteControlProtocolDMX::ProcessAndApplyProtocolValue(const FDMXSignalSharedPtr& InSignal, int32 InDMXOffset, const FRemoteControlProtocolEntityPtr& InProtocolEntityPtr)
{
	if (!InSignal->ChannelData.IsValidIndex(InDMXOffset))
	{
		return;
	}
	
	FRemoteControlDMXProtocolEntity* DMXProtocolEntity = InProtocolEntityPtr->CastChecked<FRemoteControlDMXProtocolEntity>();
	const uint8* ChannelData = &InSignal->ChannelData[InDMXOffset];	
	const uint8 NumChannelsToOccupy = UDMXEntityFixtureType::NumChannelsToOccupy(DMXProtocolEntity->DataType);

	if(DMXProtocolEntity->CacheDMXBuffer.Num() != NumChannelsToOccupy ||
		FMemory::Memcmp(DMXProtocolEntity->CacheDMXBuffer.GetData(), ChannelData, NumChannelsToOccupy) != 0)
	{
		const uint32 DMXValue = UDMXEntityFixtureType::BytesToInt(DMXProtocolEntity->DataType, DMXProtocolEntity->bUseLSB, ChannelData);
		
#if WITH_EDITOR
		FRemoteControlLogger::Get().Log(ProtocolName, [&InSignal, DMXValue]
		{
			return FText::Format(LOCTEXT("DMXEventLog","ExternUniverseID {0}, DMXValue {1}"), InSignal->ExternUniverseID, DMXValue);
		});
#endif
	
		QueueValue(InProtocolEntityPtr, DMXValue);

		// update cached buffer
		DMXProtocolEntity->CacheDMXBuffer = TArray<uint8>(ChannelData, NumChannelsToOccupy);
	}
}

void FRemoteControlProtocolDMX::UnbindAll()
{
	ProtocolsBindings.Empty();
}

#undef LOCTEXT_NAMESPACE
