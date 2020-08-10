// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithDirectLink.h"

#include "DirectLink/Network/DirectLinkMessages.h"
#include "DirectLink/Network/DirectLinkScenePipe.h"
#include "DirectLink/SceneIndex.h"
#include "DirectLink/SceneIndexBuilder.h"
#include "IDatasmithSceneElements.h"
#include "DatasmithExporterManager.h"

#include "Containers/Ticker.h"
#include "MeshDescription.h"
#include "MessageEndpointBuilder.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogDatasmithDirectLinkExporterAPI);

using DirectLink::NewMessage;


class FDatasmithDirectLinkImpl
{
public:

	static int32 ValidateCommunicationSetup()
	{
		return
			(FModuleManager::Get().LoadModule(TEXT("Messaging")) ? 0 : 1) +
			(FModuleManager::Get().LoadModule(TEXT("UdpMessaging")) ? 0 : 2) +
			(FModuleManager::Get().LoadModule(TEXT("Networking")) ? 0 : 3) +
			(FParse::Param(FCommandLine::Get(), TEXT("Messaging")) ? 0 : 4);
	}

	FDatasmithDirectLinkImpl()
		: Endpoint(TEXT("DatasmithExporter"))
	{
		Endpoint.SetVerbose();
		check(ValidateCommunicationSetup() == 0);
		// #ue_directlink_integration app specific endpoint name, and source name.
	}

	bool InitializeForScene(TSharedRef<IDatasmithScene>& Scene)
	{
		UE_LOG(LogDatasmithDirectLinkExporterAPI, Log, TEXT("InitializeForScene"));

		DirectLink::FIndexedScene IndexedScene(&Scene.Get());

		check(Scene->GetSharedState().IsValid());

		Endpoint.RemoveSource(Source);
		Source = Endpoint.AddSource(TEXT("exporter-noname"), DirectLink::EVisibility::Public);
		Endpoint.SetSourceRoot(Source, &*Scene, false);
		CurrentScene = Scene;

		return true;
	}

	bool UpdateScene(TSharedRef<IDatasmithScene>& Scene)
	{
		UE_LOG(LogDatasmithDirectLinkExporterAPI, Log, TEXT("UpdateScene"));

		if (CurrentScene != Scene)
		{
			InitializeForScene(Scene);
		}

		if (!CurrentScene || !CurrentScene->GetSharedState().IsValid())
		{
			UE_LOG(LogDatasmithDirectLinkExporterAPI, Warning,
				TEXT("UpdateScene issue: no shared state on given scene"));
			return false;
		}

		Endpoint.SnapshotSource(Source);
		return true;
	}

private:
	DirectLink::FEndpoint Endpoint;
	DirectLink::FSourceHandle Source;
	TSharedPtr<IDatasmithScene> CurrentScene;
};


TUniquePtr<class FDatasmithDirectLinkImpl> DirectLinkImpl;

int32 FDatasmithDirectLink::ValidateCommunicationSetup()
{ return FDatasmithDirectLinkImpl::ValidateCommunicationSetup(); }

bool FDatasmithDirectLink::Shutdown()
{
	DirectLinkImpl.Reset();
	FDatasmithExporterManager::Shutdown();
	return true;
}

FDatasmithDirectLink::FDatasmithDirectLink()
{
	if (!DirectLinkImpl)
	{
		DirectLinkImpl = MakeUnique<FDatasmithDirectLinkImpl>();
	}
}

FDatasmithDirectLink::~FDatasmithDirectLink() = default;

bool FDatasmithDirectLink::InitializeForScene(TSharedRef<IDatasmithScene>& Scene)
{ return DirectLinkImpl->InitializeForScene(Scene); }

bool FDatasmithDirectLink::UpdateScene(TSharedRef<IDatasmithScene>& Scene)
{ return DirectLinkImpl->UpdateScene(Scene); }
