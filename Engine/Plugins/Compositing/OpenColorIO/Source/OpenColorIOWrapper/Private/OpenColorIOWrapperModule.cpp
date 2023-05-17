// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOWrapperModule.h"

#include "OpenColorIOWrapper.h"
#include "OpenColorIOLibHandler.h"


DEFINE_LOG_CATEGORY(LogOpenColorIOWrapper);

#define OPENCOLORIOWRAPPER_MODULE_NAME "OpenColorIOWrapper"
#define LOCTEXT_NAMESPACE "OpenColorIOWrapperModule"

class FOpenColorIOWrapperModule : public IOpenColorIOWrapperModule
{
public:
	//~ Begin IModuleInterface interface
	virtual void StartupModule() override
	{
		FOpenColorIOLibHandler::Initialize();

		EngineBuiltInConfig = MakeUnique<FOpenColorIOEngineBuiltInConfigWrapper>();
	}

	virtual void ShutdownModule() override
	{
		FOpenColorIOLibHandler::Shutdown();
	}
	//~ End IModuleInterface interface

	//~ Begin IOpenColorIOWrapperModule interface
	virtual FOpenColorIOEngineBuiltInConfigWrapper* GetEngineBuiltInConfig() override
	{
		return EngineBuiltInConfig.Get();
	}

	virtual const FOpenColorIOEngineBuiltInConfigWrapper* GetEngineBuiltInConfig() const override
	{
		return EngineBuiltInConfig.Get();
	}
	//~ End IOpenColorIOWrapperModule interface
private:
	
	TUniquePtr<FOpenColorIOEngineBuiltInConfigWrapper> EngineBuiltInConfig = nullptr;
};

IMPLEMENT_MODULE(FOpenColorIOWrapperModule, OpenColorIOWrapper);

#undef LOCTEXT_NAMESPACE
