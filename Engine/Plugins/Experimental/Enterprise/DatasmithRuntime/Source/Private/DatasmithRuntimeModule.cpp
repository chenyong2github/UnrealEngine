// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntimeModule.h"

#include "MaterialSelectors/DatasmithRevitLiveMaterialSelector.h"

#include "DatasmithTranslatorModule.h"
#include "DirectLink/Network/DirectLinkEndpoint.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"

class FDatasmithRuntimeModule : public IDatasmithRuntimeModuleInterface
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
		ReceiverEndpoint.Reset();
		FDatasmithMasterMaterialManager::Get().UnregisterSelector(TEXT("Revit"));
	}

	bool RegisterSceneProvider(TSharedPtr<DirectLink::ISceneProvider> SceneProvider)
	{
		if (!SceneProvider.IsValid())
		{
			return false;
		}

		if (!ReceiverEndpoint.IsValid())
		{
			ReceiverEndpoint.Reset();
			bool bInit = true;
#if !WITH_EDITOR
			bInit = (FModuleManager::Get().LoadModule(TEXT("Messaging")))
				 && (FModuleManager::Get().LoadModule(TEXT("Networking")))
				 && (FModuleManager::Get().LoadModule(TEXT("UdpMessaging")));
#endif

			if (bInit)
			{
				ReceiverEndpoint = MakeUnique<DirectLink::FEndpoint>(TEXT("Datasmith-DatasmithRuntime"));
				ReceiverEndpoint->SetVerbose();
				Destination = ReceiverEndpoint->AddDestination(TEXT("exporter-noname"), DirectLink::EVisibility::Public, SceneProvider);
				return true;
			}

		}

		return false;
	}

	void UnregisterSceneProvider(TSharedPtr<DirectLink::ISceneProvider> SceneProvider)
	{
		if (SceneProvider.IsValid() && ReceiverEndpoint.IsValid())
		{
			ReceiverEndpoint->RemoveDestination(Destination);
			ReceiverEndpoint.Reset();
		}
	}

private:

	TUniquePtr<DirectLink::FEndpoint> ReceiverEndpoint;
	DirectLink::FDestinationHandle Destination;
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FDatasmithRuntimeModule, DatasmithRuntime);

