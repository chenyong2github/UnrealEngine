// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorModule.h"
#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorCommands.h"
#include "DisplayClusterConfiguratorAssetTypeActions.h"

#include "AssetToolsModule.h"
#include "Modules/ModuleManager.h"

#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "DisplayClusterConfigurator"


void FDisplayClusterConfiguratorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	ConfiguratorAssetTypeAction = MakeShared<FDisplayClusterConfiguratorAssetTypeActions>();
	AssetTools.RegisterAssetTypeActions(ConfiguratorAssetTypeAction.ToSharedRef());

	FDisplayClusterConfiguratorStyle::Initialize();
	FDisplayClusterConfiguratorCommands::Register();
}

void FDisplayClusterConfiguratorModule::ShutdownModule()
{
	if (FAssetToolsModule* AssetTools = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		AssetTools->Get().UnregisterAssetTypeActions(ConfiguratorAssetTypeAction.ToSharedRef());
	}

	FDisplayClusterConfiguratorStyle::Shutdown();
}

const FDisplayClusterConfiguratorCommands& FDisplayClusterConfiguratorModule::GetCommands() const
{
	return FDisplayClusterConfiguratorCommands::Get();
}

IMPLEMENT_MODULE(FDisplayClusterConfiguratorModule, DisplayClusterConfigurator);

#undef LOCTEXT_NAMESPACE
