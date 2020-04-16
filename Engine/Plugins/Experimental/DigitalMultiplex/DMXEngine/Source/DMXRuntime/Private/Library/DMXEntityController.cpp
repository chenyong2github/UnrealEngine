// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntityController.h"
#include "Interfaces/IDMXProtocol.h"

#if WITH_EDITOR

void UDMXEntityController::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName&& PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, UniverseLocalStart)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, UniverseLocalNum)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, UniverseRemoteStart)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, DeviceProtocol))
	{
		ValidateRangeValues();
		UpdateUniversesFromRange();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UDMXEntityController::PostLoad()
{
	Super::PostLoad();
	ValidateRangeValues();
	UpdateUniversesFromRange();
}

void UDMXEntityController::PostInitProperties()
{
	Super::PostInitProperties();
	ValidateRangeValues();
	UpdateUniversesFromRange();
}

#endif // WITH_EDITOR

void UDMXEntityController::ValidateRangeValues()
{
	if (DeviceProtocol.IsValid())
	{
		const IDMXProtocolPtr& Protocol = DeviceProtocol;
		const int32 MinUniverseID = Protocol->GetMinUniverseID();
		const int32 MaxUniverseID = Protocol->GetMaxUniverses();

		// To make sure all protocols have a minimum local value of 1, we offset their min values
		const int32 LocalMinOffset = 1 - MinUniverseID;

		// Clamp local values
		UniverseLocalStart = FMath::Clamp(UniverseLocalStart, 1, MaxUniverseID + LocalMinOffset);
		UniverseLocalNum = FMath::Clamp(UniverseLocalNum, 1, MaxUniverseID + LocalMinOffset - UniverseLocalStart + 1);

		UniverseLocalEnd = UniverseLocalStart + UniverseLocalNum - 1;

		// Clamp remote start to have valid remote range
		UniverseRemoteStart = FMath::Clamp(UniverseRemoteStart, MinUniverseID, MaxUniverseID - UniverseLocalNum + 1);
	}
	else
	{
		if (UniverseLocalStart < 0)
		{
			UniverseLocalStart = 0; 
		}
		if (UniverseLocalNum < 1)
		{
			UniverseLocalNum = 1; 
		}

		UniverseLocalEnd = UniverseLocalStart + UniverseLocalNum - 1;
	}

	UniverseRemoteEnd = UniverseRemoteStart + UniverseLocalNum - 1;
	RemoteOffset = UniverseRemoteStart - UniverseLocalStart;
}

void UDMXEntityController::UpdateUniversesFromRange()
{
	const uint16 NumUniverses(UniverseLocalNum);

	if (NumUniverses < Universes.Num())
	{
		Universes.Reset(NumUniverses);
		Universes.AddZeroed(NumUniverses);
	}
	else if (NumUniverses > Universes.Num())
	{
		Universes.AddZeroed(NumUniverses - Universes.Num());
	}

	for (uint16 UniverseIndex = 0, UniverseID = UniverseRemoteStart; UniverseID <= UniverseRemoteEnd; ++UniverseIndex, ++UniverseID)
	{
		Universes[UniverseIndex].UniverseNumber = UniverseID;
	}
}
