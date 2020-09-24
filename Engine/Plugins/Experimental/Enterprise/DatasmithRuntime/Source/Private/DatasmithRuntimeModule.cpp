// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntimeModule.h"

#include "DatasmithRuntimeBlueprintLibrary.h"

#include "DatasmithTranslatorModule.h"
#include "DirectLink/Network/DirectLinkEndpoint.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"
#include "MaterialSelectors/DatasmithRevitLiveMaterialSelector.h"

class FDatasmithRuntimeModule : public IDatasmithRuntimeModuleInterface, public DirectLink::IEndpointObserver
{
public:
	virtual void StartupModule() override
	{
		// Verify DatasmithTranslatorModule has been loaded
		check(IDatasmithTranslatorModule::IsAvailable());

		// Overwrite Revit material selector with the one of DatasmithRuntime
		FDatasmithMasterMaterialManager::Get().RegisterSelector(TEXT("Revit"), MakeShared< FDatasmithRevitLiveMaterialSelector >());

		FModuleManager::Get().LoadModule(TEXT("UdpMessaging"));
	}

	virtual void ShutdownModule() override
	{
		FDatasmithMasterMaterialManager::Get().UnregisterSelector(TEXT("Revit"));
	}

private:
	DirectLink::FRawInfo LastRawInfo;
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FDatasmithRuntimeModule, DatasmithRuntime);

