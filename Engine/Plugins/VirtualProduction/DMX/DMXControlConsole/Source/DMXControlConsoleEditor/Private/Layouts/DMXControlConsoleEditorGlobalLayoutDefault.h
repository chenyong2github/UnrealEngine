// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Layouts/DMXControlConsoleEditorGlobalLayoutBase.h"

#include "DMXControlConsoleEditorGlobalLayoutDefault.generated.h"

class UDMXControlConsoleData;
class UDMXEntity;
class UDMXLibrary;


/** A layout where Control Console data are sorted by default */
UCLASS()
class UDMXControlConsoleEditorGlobalLayoutDefault
	: public UDMXControlConsoleEditorGlobalLayoutBase
{
	GENERATED_BODY()

public:
	//~ Begin UDMXControlConsoleBaseGlobalLayout interface
	virtual void GenerateLayoutByControlConsoleData(const UDMXControlConsoleData* ControlConsoleData) override;
	//~ End UDMXControlConsoleBaseGlobalLayout interface

	/** Called when a Fixture Patch was removed from a DMX Library */
	void OnFixturePatchRemovedFromLibrary(UDMXLibrary* Library, TArray<UDMXEntity*> Entities);
	
	/** Called when a Fader Group was added to Control Console Data */
	void OnFaderGroupAddedToData(const UDMXControlConsoleFaderGroup* FaderGroup, UDMXControlConsoleData* ControlConsoleData);

	/** Called to clean this layout from all unpatched Fader Groups */
	void CleanLayoutFromUnpatchedFaderGroups();
};
