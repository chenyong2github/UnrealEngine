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

		InterchangeConfig = FOpenColorIOConfigWrapper::CreateWorkingColorSpaceToInterchangeConfig();
	}

	virtual void ShutdownModule() override
	{
		FOpenColorIOLibHandler::Shutdown();
	}
	//~ End IModuleInterface interface

	//~ Begin IOpenColorIOWrapperModule interface
	virtual const FOpenColorIOConfigWrapper* GetWorkingColorSpaceToInterchangeConfig() const override
	{
		return InterchangeConfig.Get();
	}

	virtual void LoadGlobalConfig(FStringView InFilePath) override
	{
		FOpenColorIOConfigWrapper::FInitializationOptions Opt;
		Opt.bAddWorkingColorSpace = true;
		EngineConfig = MakeUnique<FOpenColorIOConfigWrapper>(InFilePath, Opt);
	}

	virtual const FOpenColorIOConfigWrapper* GetGlobalConfig() const override
	{
		return EngineConfig.Get();
	}
	//~ End IOpenColorIOWrapperModule interface
private:
	
	TUniquePtr<FOpenColorIOConfigWrapper> InterchangeConfig = nullptr;
	TUniquePtr<FOpenColorIOConfigWrapper> EngineConfig = nullptr;
};

IMPLEMENT_MODULE(FOpenColorIOWrapperModule, OpenColorIOWrapper);

#undef LOCTEXT_NAMESPACE
