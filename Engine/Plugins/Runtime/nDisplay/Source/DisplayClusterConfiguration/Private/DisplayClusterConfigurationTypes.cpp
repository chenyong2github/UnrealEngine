// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfigurationLog.h"
#include "Engine/StaticMesh.h"

UDisplayClusterConfigurationData::UDisplayClusterConfigurationData()
{
	Scene   = CreateDefaultSubobject<UDisplayClusterConfigurationScene>(TEXT("Scene"));
	Cluster = CreateDefaultSubobject<UDisplayClusterConfigurationCluster>(TEXT("Cluster"));
	Input   = CreateDefaultSubobject<UDisplayClusterConfigurationInput>(TEXT("Input"));
}

const UDisplayClusterConfigurationClusterNode* UDisplayClusterConfigurationData::GetClusterNode(const FString& NodeId) const
{
	return Cluster->Nodes.Contains(NodeId) ? Cluster->Nodes[NodeId] : nullptr;
}

const UDisplayClusterConfigurationViewport* UDisplayClusterConfigurationData::GetViewport(const FString& NodeId, const FString& ViewportId) const
{
	const UDisplayClusterConfigurationClusterNode* Node = GetClusterNode(NodeId);
	if (Node)
	{
		return Node->Viewports.Contains(ViewportId) ? Node->Viewports[ViewportId] : nullptr;
	}

	return nullptr;
}

bool UDisplayClusterConfigurationData::GetPostprocess(const FString& NodeId, const FString& PostprocessId, FDisplayClusterConfigurationPostprocess& OutPostprocess) const
{
	const UDisplayClusterConfigurationClusterNode* Node = GetClusterNode(NodeId);
	if (Node)
	{
		const FDisplayClusterConfigurationPostprocess* PostprocessOperation = Node->Postprocess.Find(PostprocessId);
		if (PostprocessOperation)
		{
			OutPostprocess = *PostprocessOperation;
			return true;
		}
	}

	return false;
}

bool UDisplayClusterConfigurationData::GetProjectionPolicy(const FString& NodeId, const FString& ViewportId, FDisplayClusterConfigurationProjection& OutProjection) const
{
	const UDisplayClusterConfigurationViewport* Viewport = GetViewport(NodeId, ViewportId);
	if (Viewport)
	{
		OutProjection = Viewport->ProjectionPolicy;
		return true;
	}

	return false;
}

#if WITH_EDITOR
void UDisplayClusterConfigurationViewport::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	OnPostEditChangeChainProperty.Broadcast(PropertyChangedEvent);
}

void UDisplayClusterConfigurationClusterNode::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	OnPostEditChangeChainProperty.Broadcast(PropertyChangedEvent);
}

void UDisplayClusterConfigurationSceneComponentMesh::LoadAssets()
{
	Asset = nullptr;
	
	if (AssetPath.Contains(TEXT("//"), ESearchCase::CaseSensitive))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Attempted to create a package with name containing double slashes. PackageName: %s"), *AssetPath);
	}
	else
	{
		Asset = LoadObject<UStaticMesh>(nullptr, *AssetPath, nullptr, LOAD_Quiet | LOAD_NoWarn);

		if (Asset == nullptr)
		{
			UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Can't load asset with PackageName %s"), *AssetPath);
		}
	}
}
#endif