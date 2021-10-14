// Copyright Epic Games, Inc. All Rights Reserved.

#include "nDisplaySupportModule.h"

#include "ILevelSnapshotsModule.h"
#include "Map/DisplayClusterConfigurationClusterNodeSerializer.h"
#include "Map/DisplayClusterConfigurationClusterSerializer.h"
#include "Reference/DisplayClusterConfigurationDataSerializer.h"
#include "Reference/DisplayClusterRootActorSerializer.h"

#define LOCTEXT_NAMESPACE "FnDisplaySupportForLevelSnapshotsModule"

void FnDisplaySupportModule::StartupModule()
{
	FModuleManager& ModuleManager = FModuleManager::Get();
	const bool bIsNDisplayLoaded = ModuleManager.IsModuleLoaded("DisplayCluster");
	if (bIsNDisplayLoaded)
	{
		ILevelSnapshotsModule& LevelSnapshotsModule = ModuleManager.LoadModuleChecked<ILevelSnapshotsModule>("LevelSnapshots");
		
		FDisplayClusterRootActorSerializer::Register(LevelSnapshotsModule);
		FDisplayClusterConfigurationDataSerializer::Register(LevelSnapshotsModule);
		FDisplayClusterConfigurationClusterSerializer::Register(LevelSnapshotsModule);
		FDisplayClusterConfigurationClusterNodeSerializer::Register(LevelSnapshotsModule);
	}
}
void FnDisplaySupportModule::ShutdownModule()
{}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FnDisplaySupportModule, nDisplaySupportForLevelSnapshots)