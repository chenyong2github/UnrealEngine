// Copyright Epic Games, Inc. All Rights Reserved.
#include "IAudioParameterTransmitter.h"
#include "UObject/Object.h"


namespace Audio
{
	const FName IParameterTransmitter::RouterName = "ParameterTransmitter";

	bool ILegacyParameterTransmitter::GetParameter(FName InName, FAudioParameter& OutValue) const
	{
		return false;
	}

	TArray<UObject*> ILegacyParameterTransmitter::GetReferencedObjects() const
	{
		return { };
	}
} // namespace Audio