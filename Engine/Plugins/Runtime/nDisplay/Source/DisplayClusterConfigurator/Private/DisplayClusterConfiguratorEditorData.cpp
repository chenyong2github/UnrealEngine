// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorEditorData.h"

const TSet<FString> UDisplayClusterConfiguratorEditorData::RenderSyncPolicies =
{
	TEXT("Ethernet"),
	TEXT("Nvidia"),
	TEXT("None")
};

const TSet<FString> UDisplayClusterConfiguratorEditorData::InputSyncPolicies =
{
	TEXT("ReplicateMaster"),
	TEXT("None")
};

const TSet<FString> UDisplayClusterConfiguratorEditorData::ProjectionPoliсies =
{
	TEXT("Simple"),
	TEXT("Camera"),
	TEXT("MPCDI"),
	TEXT("Mesh"),
	TEXT("EasyBlend"),
	TEXT("DomeProjection"),
	TEXT("Manual"),
	TEXT("PICP_MPCDI"),
	TEXT("PICP_Mesh"),
};

UDisplayClusterConfiguratorEditorData::UDisplayClusterConfiguratorEditorData()
{
}
