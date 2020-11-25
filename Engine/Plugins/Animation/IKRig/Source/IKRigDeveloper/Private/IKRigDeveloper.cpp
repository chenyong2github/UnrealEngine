// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDeveloper.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "IKRigDeveloperModule"

class FIKRigDeveloperModule : public IIKRigDeveloperModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

void FIKRigDeveloperModule::StartupModule()
{
}

void FIKRigDeveloperModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FIKRigDeveloperModule, IKRigDeveloper)
#undef LOCTEXT_NAMESPACE
