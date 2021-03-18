// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

/** Implements the DMXProtocolEditor Module  */
class DMXPROTOCOLEDITOR_API FDMXProtocolEditorModule 
	: public IModuleInterface
{
public:
	//~ Begin IModuleInterface implementation
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface implementation

	/** Get the instance of this module. */
	static FDMXProtocolEditorModule& Get();

private:
	/** Called when all protocols are registered */
	void OnProtocolsRegistered();

	/** Registers details customizations */
	void RegisterDetailsCustomizations();

	/** Unregisters details customizations */
	void UnregisterDetailsCustomizations();
};
