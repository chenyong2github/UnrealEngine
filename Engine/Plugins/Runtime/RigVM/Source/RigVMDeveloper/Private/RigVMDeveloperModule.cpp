// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RigVMDeveloper.h: Module implementation.
=============================================================================*/

#include "RigVMDeveloperModule.h"
#include "Modules/ModuleManager.h"
#include "RigVMBlueprint.h"

DEFINE_LOG_CATEGORY(LogRigVMDeveloper);

class FRigVMDeveloperModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FRigVMDeveloperModule, RigVMDeveloper);


