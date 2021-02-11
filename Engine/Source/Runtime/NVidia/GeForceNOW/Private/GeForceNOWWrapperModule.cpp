// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IGeForceNOWWrapperModule.h"
#include "GeForceNOWWrapperPrivate.h"

DEFINE_LOG_CATEGORY(LogGeForceNow);

class FGeForceNOWWrapperModule : public IGeForceNOWWrapperModule
{
public:

	FGeForceNOWWrapperModule(){}

	virtual void StartupModule() override{}
	virtual void ShutdownModule() override{}

};

IMPLEMENT_MODULE(FGeForceNOWWrapperModule, GeForceNOWWrapper);
