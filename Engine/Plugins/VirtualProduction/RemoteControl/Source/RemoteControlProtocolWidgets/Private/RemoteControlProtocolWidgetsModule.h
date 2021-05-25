// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlProtocolWidgetsModule.h"

REMOTECONTROLPROTOCOLWIDGETS_API DECLARE_LOG_CATEGORY_EXTERN(LogRemoteControlProtocolWidgets, Log, All);

class IRCProtocolBindingList;
class URemoteControlPreset;

class FRemoteControlProtocolWidgetsModule : public IRemoteControlProtocolWidgetsModule
{
public:
	//~ Begin IRemoteControlProtocolWidgetsModule Interface
	virtual TSharedRef<SWidget> GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId, const EExposedFieldType& InFieldType) override;
	virtual void ResetProtocolBindingList() override;
	virtual TSharedPtr<IRCProtocolBindingList> GetProtocolBindingList() const override;
	//~ End IRemoteControlProtocolWidgetsModule Interface

private:
	/** Binding list public interface instance */
	TSharedPtr<IRCProtocolBindingList> RCProtocolBindingList;
};
