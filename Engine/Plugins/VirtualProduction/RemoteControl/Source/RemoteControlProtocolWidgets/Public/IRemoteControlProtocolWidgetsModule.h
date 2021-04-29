// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "RemoteControlField.h"

class SWidget;
class URemoteControlPreset;
class SWidget;

/** A Remote Control module that provides editor widgets for protocol bindings. */
class IRemoteControlProtocolWidgetsModule : public IModuleInterface
{
public:
	/** Creates a widget for the given Preset Field and FieldType */
	virtual TSharedRef<SWidget> GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId, const EExposedFieldType& InFieldType = EExposedFieldType::Invalid) = 0;
};
