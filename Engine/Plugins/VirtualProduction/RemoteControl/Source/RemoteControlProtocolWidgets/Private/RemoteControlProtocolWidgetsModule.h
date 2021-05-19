// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlProtocolWidgetsModule.h"

REMOTECONTROLPROTOCOLWIDGETS_API DECLARE_LOG_CATEGORY_EXTERN(LogRemoteControlProtocolWidgets, Log, All);

class URemoteControlPreset;

class FRemoteControlProtocolWidgetsModule : public IRemoteControlProtocolWidgetsModule
{
public:
	/** Creates a widget for the given Preset Field */
	virtual TSharedRef<SWidget> GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId, const EExposedFieldType& InFieldType) override;
};
