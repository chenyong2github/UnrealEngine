// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkOverNDisplayModule.h"

#include "DisplayClusterGameEngine.h"
#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "IDisplayCluster.h"
#include "ILiveLinkClient.h"
#include "LiveLinkOverNDisplayPrivate.h"
#include "LiveLinkOverNDisplaySettings.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "NDisplayLiveLinkSubjectReplicator.h"

#if WITH_EDITOR
	#include "ISettingsModule.h"
	#include "ISettingsSection.h"
#endif 


DEFINE_LOG_CATEGORY(LogLiveLinkOverNDisplay);

#define LOCTEXT_NAMESPACE "LiveLinkOverNDisplayModule"


FLiveLinkOverNDisplayModule::FLiveLinkOverNDisplayModule()
	: LiveLinkReplicator(MakeUnique<FNDisplayLiveLinkSubjectReplicator>())
{

}

void FLiveLinkOverNDisplayModule::StartupModule()
{
	// Register Engine callbacks
	OnEngineLoopInitCompleteDelegate = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FLiveLinkOverNDisplayModule::OnEngineLoopInitComplete);

#if WITH_EDITOR
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings(
			"Project", "Plugins", "LiveLink over nDisplay",
			LOCTEXT("LiveLinkOverNDisplaySettingsName", "LiveLink over nDisplay"),
			LOCTEXT("LiveLinkOverNDisplaySettingsDescription", "Configure LiveLink over nDisplay."),
			GetMutableDefault<ULiveLinkOverNDisplaySettings>()
		);
	}
#endif
}

void FLiveLinkOverNDisplayModule::ShutdownModule()
{
#if WITH_EDITOR
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "LiveLink over nDisplay");
	}
#endif

	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);
}

FNDisplayLiveLinkSubjectReplicator& FLiveLinkOverNDisplayModule::GetSubjectReplicator()
{
	return *LiveLinkReplicator;
}

void FLiveLinkOverNDisplayModule::OnEngineLoopInitComplete()
{
	if (UDisplayClusterGameEngine* DisplayClusterEngine = Cast<UDisplayClusterGameEngine>(GEngine))
	{
		if (EDisplayClusterOperationMode::Cluster == DisplayClusterEngine->GetOperationMode())
		{
			if (GetDefault<ULiveLinkOverNDisplaySettings>()->IsLiveLinkOverNDisplayEnabled())
			{
				LiveLinkReplicator->Activate();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE(FLiveLinkOverNDisplayModule, LiveLinkOverNDisplay);

