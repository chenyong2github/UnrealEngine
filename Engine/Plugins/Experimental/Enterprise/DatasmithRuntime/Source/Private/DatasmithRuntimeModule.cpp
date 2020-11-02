// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntimeModule.h"

#include "DatasmithRuntime.h"
#include "DirectLinkUtils.h"

#include "DatasmithTranslatorModule.h"

class FDatasmithRuntimeModule : public IDatasmithRuntimeModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Verify DatasmithTranslatorModule has been loaded
		check(IDatasmithTranslatorModule::IsAvailable());

		FModuleManager::Get().LoadModuleChecked(TEXT("UdpMessaging"));

		DatasmithRuntime::FDestinationProxy::InitializeEndpointProxy();

		ADatasmithRuntimeActor::OnStartupModule();
	}

	virtual void ShutdownModule() override
	{
		ADatasmithRuntimeActor::OnShutdownModule();

		DatasmithRuntime::FDestinationProxy::ShutdownEndpointProxy();
	}

};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FDatasmithRuntimeModule, DatasmithRuntime);

