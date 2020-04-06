// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkMessageBusSourceSettings.h"

#include "LiveLinkSettings.h"
#include "UObject/UObjectGlobals.h"

ULiveLinkMessageBusSourceSettings::ULiveLinkMessageBusSourceSettings()
{
	Mode = GetDefault<ULiveLinkSettings>()->DefaultMessageBusSourceMode;
}
