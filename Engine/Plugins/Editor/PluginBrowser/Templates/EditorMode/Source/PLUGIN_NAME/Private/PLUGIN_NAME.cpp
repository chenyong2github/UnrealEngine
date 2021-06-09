// Copyright Epic Games, Inc. All Rights Reserved.

#include "PLUGIN_NAME.h"
#include "Modules/ModuleInterface.h"

class FPLUGIN_NAMEModule : public IModuleInterface
{
	/** This function will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module */
	//virtual void StartupModule() override;

	/** This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	 *  we call this function before unloading the module
	 */
	//virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FPLUGIN_NAMEModule, PLUGIN_NAME)