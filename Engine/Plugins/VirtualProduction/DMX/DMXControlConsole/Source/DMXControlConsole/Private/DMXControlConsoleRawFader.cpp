// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleRawFader.h"


UDMXControlConsoleRawFader::UDMXControlConsoleRawFader()
{
	FaderName = TEXT("Fader");
}

#if WITH_EDITOR
bool UDMXControlConsoleRawFader::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty->NamePrivate == GetDataTypePropertyName() ||
		InProperty->NamePrivate == GetUniverseIDPropertyName() ||
		InProperty->NamePrivate == GetStartingAddressPropertyName() ||
		InProperty->NamePrivate == GetUseLSBModePropertyName())
	{
		return true;
	}
	else
	{
		return Super::CanEditChange(InProperty);
	}
}
#endif // WITH_EDITOR
