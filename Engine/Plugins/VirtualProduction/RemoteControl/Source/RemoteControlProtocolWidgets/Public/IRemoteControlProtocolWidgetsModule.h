// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class URemoteControlPreset;

/** A Remote Control module that provides editor widgets for protocol bindings. */
class IRemoteControlProtocolWidgetsModule : public IModuleInterface
{
public:
	/** Creates a widget for the given Preset Field */
	virtual TSharedRef<SWidget> GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId) = 0;
};
