// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteSessionEditorModule.h"
#include "RemoteSessionEditorStyle.h"

#include "Modules/ModuleManager.h"
#include "Widgets/SRemoteSessionStream.h"

#define LOCTEXT_NAMESPACE "FRemoteSessionEditorModule"


class FRemoteSessionEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FRemoteSessionEditorStyle::Register();
		SRemoteSessionStream::RegisterNomadTabSpawner();
	}

	virtual void ShutdownModule() override
	{
		if (!IsEngineExitRequested() && UObjectInitialized())
		{
			FRemoteSessionEditorStyle::Unregister();
			SRemoteSessionStream::UnregisterNomadTabSpawner();
		}
	}
};

IMPLEMENT_MODULE(FRemoteSessionEditorModule, RemoteSessionEditor)

#undef LOCTEXT_NAMESPACE
