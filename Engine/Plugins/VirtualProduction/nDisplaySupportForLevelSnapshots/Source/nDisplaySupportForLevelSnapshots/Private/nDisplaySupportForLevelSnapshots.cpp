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
	UE_LOG(LogTemp, Log, TEXT("Starting Level Snapshot support for nDisplay..."));
	
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
{}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FnDisplaySupportForLevelSnapshotsModule, nDisplaySupportForLevelSnapshots)