// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntityController.h"
#include "Interfaces/IDMXProtocol.h"

#if WITH_EDITOR

void UDMXEntityController::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName&& PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, RemoteOffset)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, UniverseLocalStart)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, UniverseLocalNum)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, DeviceProtocol))
	{
		ValidateRangeValues();
		UpdateUniversesFromRange();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR

void UDMXEntityController::ValidateRangeValues()
{
	if (DeviceProtocol.IsValid())
	{
		const TSharedPtr<IDMXProtocol>& Protocol = DeviceProtocol;
		const int32 MinUniverseID = Protocol->GetMinUniverseID();
		const int32 MaxUniverseID = Protocol->GetMaxUniverses();

		// Clamp local values
		UniverseLocalStart = FMath::Clamp(UniverseLocalStart, MinUniverseID, MaxUniverseID);
		UniverseLocalNum = FMath::Clamp(UniverseLocalNum, 1, MaxUniverseID - UniverseLocalStart + 1);

		UniverseLocalEnd = UniverseLocalStart + UniverseLocalNum - 1;

		// Clamp remote offset to have valid remote range
		RemoteOffset = FMath::Clamp(RemoteOffset, MinUniverseID - UniverseLocalStart, MaxUniverseID - UniverseLocalStart);
		// Offset can't make UniverseRemoteEnd overflow the max Universe ID
		RemoteOffset = FMath::Min(RemoteOffset, MaxUniverseID - UniverseLocalEnd);
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

	UniverseRemoteStart = UniverseLocalStart + RemoteOffset;
	UniverseRemoteEnd = UniverseLocalEnd + RemoteOffset;
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
