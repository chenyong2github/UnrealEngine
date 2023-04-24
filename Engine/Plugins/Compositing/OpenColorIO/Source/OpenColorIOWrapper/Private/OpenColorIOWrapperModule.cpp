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
		bInitializedLib = FOpenColorIOLibHandler::Initialize();
	}

	virtual void ShutdownModule() override
	{
		FOpenColorIOLibHandler::Shutdown();
		bInitializedLib = false;
	}
	//~ End IModuleInterface interface

	virtual const FOpenColorIOConfigWrapper* GetWorkingColorSpaceToInterchangeConfig() override
	{
		check(bInitializedLib);

		if (!InterchangeConfig)
		{
			InterchangeConfig = FOpenColorIOConfigWrapper::CreateWorkingColorSpaceToInterchangeConfig();
		}

		return InterchangeConfig.Get();
	}
private:

	bool bInitializedLib = false;
	
	TUniquePtr<FOpenColorIOConfigWrapper> InterchangeConfig;
};

IMPLEMENT_MODULE(FOpenColorIOWrapperModule, OpenColorIO);

#undef LOCTEXT_NAMESPACE
