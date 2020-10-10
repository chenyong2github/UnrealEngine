// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorModule.h"
#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorCommands.h"
#include "DisplayClusterConfiguratorAssetTypeActions.h"

#include "AssetToolsModule.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "DisplayClusterConfigurator"

FOnDisplayClusterConfiguratorReadOnlyChanged FDisplayClusterConfiguratorModule::OnDisplayClusterConfiguratorReadOnlyChanged;

static TAutoConsoleVariable<bool> CVarDisplayClusterConfiguratorReadOnly(
	TEXT("nDisplay.configurator.ReadOnly"),
	true,
	TEXT("Enable or disable editing functionality")
	);

static FAutoConsoleVariableSink CVarDisplayClusterConfiguratorReadOnlySink(FConsoleCommandDelegate::CreateStatic(&FDisplayClusterConfiguratorModule::ReadOnlySink));

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

void FDisplayClusterConfiguratorModule::ReadOnlySink()
{
	bool bNewDisplayClusterConfiguratorReadOnly = CVarDisplayClusterConfiguratorReadOnly.GetValueOnGameThread();

	// By default we assume the ReadOnly is true
	static bool GReadOnly = true;

	if (GReadOnly != bNewDisplayClusterConfiguratorReadOnly)
	{
		GReadOnly = bNewDisplayClusterConfiguratorReadOnly;

		// Broadcast the changes
		OnDisplayClusterConfiguratorReadOnlyChanged.Broadcast(GReadOnly);
	}
}

FDelegateHandle FDisplayClusterConfiguratorModule::RegisterOnReadOnly(const FOnDisplayClusterConfiguratorReadOnlyChangedDelegate& Delegate)
{
	return OnDisplayClusterConfiguratorReadOnlyChanged.Add(Delegate);
}

void FDisplayClusterConfiguratorModule::UnregisterOnReadOnly(FDelegateHandle DelegateHandle)
{
	OnDisplayClusterConfiguratorReadOnlyChanged.Remove(DelegateHandle);
}

IMPLEMENT_MODULE(FDisplayClusterConfiguratorModule, DisplayClusterConfigurator);

#undef LOCTEXT_NAMESPACE
