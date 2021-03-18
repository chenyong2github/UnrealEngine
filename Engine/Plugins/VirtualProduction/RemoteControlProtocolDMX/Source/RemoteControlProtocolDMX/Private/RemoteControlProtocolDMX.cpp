// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolDMX.h"

#include "DMXProtocolTypes.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXPortManager.h"
#include "Library/DMXEntityFixtureType.h"

TStatId FRemoteControlProtocolDMX::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FRemoteControlProtocolDMX, STATGROUP_Tickables);
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
					ComparedDMXProtocolEntity->StartingChannel == DMXProtocolEntity->StartingChannel &&
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

void FRemoteControlProtocolDMX::Tick(float DeltaTime)
{
	static TArray<uint8> CachedDMXValues = TArray<uint8>();
	CachedDMXValues.SetNumZeroed(1);

	if (!ProtocolsBindings.Num())
	{
		return;
	}

	const TArray<FDMXInputPortSharedRef>& InputPorts = FDMXPortManager::Get().GetInputPorts();

	// Tick all input Ports
	for (const FDMXInputPortSharedRef& InputPort : InputPorts)
	{
		// Loop through all protocol entities
		for (const FRemoteControlProtocolEntityWeakPtr& ProtocolEntityWeakPtr : ProtocolsBindings)
		{
			if (const FRemoteControlProtocolEntityPtr& ProtocolEntity = ProtocolEntityWeakPtr.Pin())
			{
				FRemoteControlDMXProtocolEntity* DMXProtocolEntity = ProtocolEntity->CastChecked<FRemoteControlDMXProtocolEntity>();

				// Get universe DMX signal
				InputPort->GameThreadGetDMXSignal(DMXProtocolEntity->Universe, DMXProtocolEntity->LastSignalPtr);

				if (DMXProtocolEntity->LastSignalPtr.IsValid())
				{					
					const int32 DMXOffset = DMXProtocolEntity->StartingChannel - 1;
					const uint8* BytesData = &DMXProtocolEntity->LastSignalPtr->ChannelData[DMXOffset];
					check(DMXOffset >= 0 && DMXOffset < DMX_UNIVERSE_SIZE);
					
					ProcessAndApplyProtocolValue(BytesData, ProtocolEntity);
				}
			}
		}
	}
}

void FRemoteControlProtocolDMX::ProcessAndApplyProtocolValue(const uint8* InChannelData, const FRemoteControlProtocolEntityPtr& InProtocolEntityPtr)
{
	FRemoteControlDMXProtocolEntity* DMXProtocolEntity = InProtocolEntityPtr->CastChecked<FRemoteControlDMXProtocolEntity>();
	const uint8 NumChannelsToOccupy = UDMXEntityFixtureType::NumChannelsToOccupy(DMXProtocolEntity->DataType);

	// Initialize cache buffer if that is 0 or resize the buffer 
	if (DMXProtocolEntity->CacheDMXBuffer.Num() == 0 || DMXProtocolEntity->CacheDMXBuffer.Num() != NumChannelsToOccupy)
	{
		DMXProtocolEntity->CacheDMXBuffer = TArray<uint8>(InChannelData, NumChannelsToOccupy);
	}

	// Apply the property if the buffer has been changed
	if (FMemory::Memcmp(DMXProtocolEntity->CacheDMXBuffer.GetData(), InChannelData, NumChannelsToOccupy) != 0)
	{
		const uint32 DMXValue = UDMXEntityFixtureType::BytesToInt(DMXProtocolEntity->DataType, DMXProtocolEntity->bUseLSB, InChannelData);

		DMXProtocolEntity->ApplyProtocolValueToProperty(DMXValue);

		// update cached buffer
		DMXProtocolEntity->CacheDMXBuffer = TArray<uint8>(InChannelData, NumChannelsToOccupy);
	}
}

void FRemoteControlProtocolDMX::UnbindAll()
{
	ProtocolsBindings.Empty();
}
