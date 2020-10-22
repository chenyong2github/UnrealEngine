// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntimeModule.h"

#include "DirectLinkUtils.h"

#include "DatasmithTranslatorModule.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"
#include "MaterialSelectors/DatasmithRuntimeRevitMaterialSelector.h"

class FDatasmithRuntimeModule : public IDatasmithRuntimeModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Verify DatasmithTranslatorModule has been loaded
		check(IDatasmithTranslatorModule::IsAvailable());

		// Overwrite Revit material selector with the one of DatasmithRuntime
		FDatasmithMasterMaterialManager::Get().RegisterSelector(TEXT("Revit"), MakeShared< FDatasmithRuntimeRevitMaterialSelector >());

		FModuleManager::Get().LoadModuleChecked(TEXT("UdpMessaging"));

		DatasmithRuntime::FDestinationProxy::InitializeEndpointProxy();
	}

	virtual void ShutdownModule() override
	{
		FDatasmithMasterMaterialManager::Get().UnregisterSelector(TEXT("Revit"));

		DatasmithRuntime::FDestinationProxy::ShutdownEndpointProxy();
	}
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FDatasmithRuntimeModule, DatasmithRuntime);

