// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "RemoteControlField.h"

class IRCProtocolBindingList;
class SWidget;
class URemoteControlPreset;
class SWidget;

/** A Remote Control module that provides editor widgets for protocol bindings. */
class IRemoteControlProtocolWidgetsModule : public IModuleInterface
{
public:
	/**
	* Singleton-like access to this module's interface.  This is just for convenience!
	* Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	*
	* @return Returns singleton instance, loading the module on demand if needed
	*/
	static IRemoteControlProtocolWidgetsModule& Get()
	{
		static const FName ModuleName = "RemoteControlProtocolWidgets";
		return FModuleManager::LoadModuleChecked<IRemoteControlProtocolWidgetsModule>(ModuleName);
	}

	/** Creates a widget for the given Preset Field and FieldType */
	virtual TSharedRef<SWidget> GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId, const EExposedFieldType& InFieldType = EExposedFieldType::Invalid) = 0;

	/** Reset protocol binding widget */
	virtual void ResetProtocolBindingList() = 0;

	/** Get the binding list public reference */
	virtual TSharedPtr<IRCProtocolBindingList> GetProtocolBindingList() const = 0;
};
