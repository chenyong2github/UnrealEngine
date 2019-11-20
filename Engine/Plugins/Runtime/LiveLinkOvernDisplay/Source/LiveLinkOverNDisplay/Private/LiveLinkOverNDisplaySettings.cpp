// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkOverNDisplaySettings.h"

#include "LiveLinkOverNDisplayPrivate.h"


ULiveLinkOverNDisplaySettings::ULiveLinkOverNDisplaySettings()
{
	FString BoolValue;
	const bool bIsFoundOnCommandLine = FParse::Value(FCommandLine::Get(), TEXT("-EnableLiveLinkOverNDisplay="), BoolValue);

	if (bIsFoundOnCommandLine)
	{
		UE_LOG(LogLiveLinkOverNDisplay, Log, TEXT("Overriding LiveLinkOverNDisplay enable flag from command line with value '%s'"), *BoolValue);
		bIsEnabledFromCommandLine = BoolValue.ToBool();
	}
}
