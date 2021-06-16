// Copyright Epic Games, Inc. All Rights Reserved.

#include "nDisplaySupportForLevelSnapshots.h"

#include "DisplayClusterConfigurationClusterNodeSerializer.h"
#include "DisplayClusterConfigurationClusterSerializer.h"
#include "DisplayClusterConfigurationDataSerializer.h"
#include "DisplayClusterRootActorSerializer.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterRootActor.h"
#include "ILevelSnapshotsModule.h"

#define LOCTEXT_NAMESPACE "FnDisplaySupportForLevelSnapshotsModule"

void FnDisplaySupportForLevelSnapshotsModule::StartupModule()
{
	ILevelSnapshotsModule& LevelSnapshotsModule = static_cast<ILevelSnapshotsModule&>(FModuleManager::Get().LoadModuleChecked("LevelSnapshots"));

	// Need to blacklist properties through which subobjects can be found
	FDisplayClusterRootActorSerializer::BlacklistCustomProperties(LevelSnapshotsModule);
	FDisplayClusterConfigurationDataSerializer::BlacklistCustomProperties(LevelSnapshotsModule);
	FDisplayClusterConfigurationClusterSerializer::BlacklistCustomProperties(LevelSnapshotsModule);
	FDisplayClusterConfigurationClusterNodeSerializer::BlacklistCustomProperties(LevelSnapshotsModule);

	LevelSnapshotsModule.RegisterCustomObjectSerializer(ADisplayClusterRootActor::StaticClass(), MakeShared<FDisplayClusterRootActorSerializer>());
	LevelSnapshotsModule.RegisterCustomObjectSerializer(UDisplayClusterConfigurationData::StaticClass(), MakeShared<FDisplayClusterConfigurationDataSerializer>());
	LevelSnapshotsModule.RegisterCustomObjectSerializer(UDisplayClusterConfigurationCluster::StaticClass(), MakeShared<FDisplayClusterConfigurationClusterSerializer>());
	LevelSnapshotsModule.RegisterCustomObjectSerializer(UDisplayClusterConfigurationClusterNode::StaticClass(), MakeShared<FDisplayClusterConfigurationClusterNodeSerializer>());
}

void FnDisplaySupportForLevelSnapshotsModule::ShutdownModule()
{
	if (!FModuleManager::Get().IsModuleLoaded("Level Snapshots"))
	{
		return;
	}
	ILevelSnapshotsModule& LevelSnapshotsModule = ILevelSnapshotsModule::Get();
	
	LevelSnapshotsModule.UnregisterCustomObjectSerializer(ADisplayClusterRootActor::StaticClass());
	LevelSnapshotsModule.UnregisterCustomObjectSerializer(UDisplayClusterConfigurationData::StaticClass());
	LevelSnapshotsModule.UnregisterCustomObjectSerializer(UDisplayClusterConfigurationCluster::StaticClass());
	LevelSnapshotsModule.UnregisterCustomObjectSerializer(UDisplayClusterConfigurationClusterNode::StaticClass());

	FDisplayClusterRootActorSerializer::UnblacklistCustomProperties(LevelSnapshotsModule);
	FDisplayClusterConfigurationDataSerializer::UnblacklistCustomProperties(LevelSnapshotsModule);
	FDisplayClusterConfigurationClusterSerializer::UnblacklistCustomProperties(LevelSnapshotsModule);
	FDisplayClusterConfigurationClusterNodeSerializer::UnblacklistCustomProperties(LevelSnapshotsModule);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FnDisplaySupportForLevelSnapshotsModule, nDisplaySupportForLevelSnapshots)