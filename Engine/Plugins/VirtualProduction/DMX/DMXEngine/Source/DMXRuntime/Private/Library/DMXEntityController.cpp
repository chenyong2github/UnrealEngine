// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntityController.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXProtocolTypes.h"

DECLARE_LOG_CATEGORY_CLASS(DMXEntityControllerLog, Log, All);


#if WITH_EDITOR
void UDMXEntityController::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName&& PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, UniverseLocalStart)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, UniverseLocalNum)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, UniverseRemoteStart)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, DeviceProtocol)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, AdditionalUnicastIPs)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDMXEntityController, CommunicationMode))
	{
		ValidateRangeValues();
		UpdateUniversesFromRange();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

#if WITH_EDITOR
bool UDMXEntityController::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty != nullptr)
	{

		FString PropertyName = InProperty->GetName();
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityController, AdditionalUnicastIPs))
		{
			return CommunicationMode != EDMXCommunicationTypes::Broadcast;
		}
	}

	return Super::CanEditChange(InProperty);
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXEntityController::PostLoad()
{
	Super::PostLoad();
	ValidateRangeValues();
	UpdateUniversesFromRange();
}
#endif // WITH_EDITOR

void UDMXEntityController::PostInitProperties()
{
	Super::PostInitProperties();
	ValidateRangeValues();
	UpdateUniversesFromRange();
}

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

	if (NumUniverses < Endpoints.Num())
	{
		Endpoints.Reset(NumUniverses);
		Endpoints.AddZeroed(NumUniverses);
	}
	else if (NumUniverses > Endpoints.Num())
	{
		Endpoints.AddZeroed(NumUniverses - Endpoints.Num());
	}

	for (uint16 UniverseIndex = 0, UniverseID = UniverseRemoteStart; UniverseID <= UniverseRemoteEnd; ++UniverseIndex, ++UniverseID)
	{
		Endpoints[UniverseIndex].UniverseNumber = UniverseID;
		if (CommunicationMode == EDMXCommunicationTypes::Unicast)
		{
			TArray<FString> UnicastIPs;
			UnicastIPs.Append(AdditionalUnicastIPs);
			Endpoints[UniverseIndex].UnicastIpAddresses = UnicastIPs;
		}
		else
		{
			Endpoints[UniverseIndex].UnicastIpAddresses = { "" };
		}
	}
}
