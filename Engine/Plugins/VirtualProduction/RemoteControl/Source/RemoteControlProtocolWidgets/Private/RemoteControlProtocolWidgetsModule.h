// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlProtocolWidgetsModule.h"
#include "Templates/SharedPointer.h"

REMOTECONTROLPROTOCOLWIDGETS_API DECLARE_LOG_CATEGORY_EXTERN(LogRemoteControlProtocolWidgets, Log, All);

class URemoteControlPreset;

class FRemoteControlProtocolWidgetsModule : public IRemoteControlProtocolWidgetsModule
{
public:
	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Creates a widget for the given Preset Field */
	virtual TSharedRef<SWidget> GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId, const EExposedFieldType& InFieldType) override;

protected:
	/** Called when any asset is loaded */
	void OnAssetLoaded(UObject* InAsset);
};
