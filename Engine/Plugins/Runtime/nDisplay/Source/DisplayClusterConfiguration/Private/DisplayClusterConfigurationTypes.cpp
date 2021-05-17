// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterConfigurationTypes_Base.h"
#include "DisplayClusterConfigurationTypes_Viewport.h"
#include "DisplayClusterConfigurationTypes_ICVFX.h"
#include "DisplayClusterConfigurationTypes_PostRender.h"

#include "DisplayClusterConfigurationLog.h"
#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterProjectionStrings.h"

#include "Formats/Text/DisplayClusterConfigurationTextTypes.h"

#include "Engine/StaticMesh.h"

#define SAVE_MAP_TO_ARRAY(Map, DestArray) \
	for (const auto& KeyVal : Map) \
	{ \
		auto Component = KeyVal.Value; \
		if(Component) \
		{ \
			DestArray.AddUnique(Component); \
		} \
	} \

#define SAVE_MAP(Map) \
	SAVE_MAP_TO_ARRAY(Map, OutObjects); \
	

FIntRect FDisplayClusterConfigurationRectangle::ToRect() const
{
	return FIntRect(FIntPoint(X, Y), FIntPoint(X + W, Y + H));
}

UDisplayClusterConfigurationData::UDisplayClusterConfigurationData()
{
	Scene   = CreateDefaultSubobject<UDisplayClusterConfigurationScene>(TEXT("Scene"));
}

const UDisplayClusterConfigurationClusterNode* UDisplayClusterConfigurationData::GetClusterNode(const FString& NodeId) const
{
	return Cluster->Nodes.Contains(NodeId) ? Cluster->Nodes[NodeId] : nullptr;
}

UDisplayClusterConfigurationViewport* UDisplayClusterConfigurationData::GetViewportConfiguration(const FString& NodeId, const FString& ViewportId)
{
	const UDisplayClusterConfigurationClusterNode* Node = GetClusterNode(NodeId);
	if (Node)
	{
		if (Node->Viewports.Contains(ViewportId))
		{
			return Node->Viewports[ViewportId];
		}
	}

	return nullptr;
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

#if WITH_EDITORONLY_DATA

const TSet<FString> UDisplayClusterConfigurationData::RenderSyncPolicies =
{
	TEXT("Ethernet"),
	TEXT("Nvidia"),
	TEXT("None")
};

const TSet<FString> UDisplayClusterConfigurationData::InputSyncPolicies =
{
	TEXT("ReplicateMaster"),
	TEXT("None")
};

const TSet<FString> UDisplayClusterConfigurationData::ProjectionPolicies =
{
	TEXT("Simple"),
	TEXT("Camera"),
	TEXT("Mesh"),
	TEXT("MPCDI"),
	TEXT("EasyBlend"),
	TEXT("DomeProjection"),
	TEXT("VIOSO"),
	TEXT("Manual"),
};

#endif

FDisplayClusterConfigurationProjection::FDisplayClusterConfigurationProjection()
{
	Type = TEXT("simple");
}

FDisplayClusterConfigurationICVFX_StageSettings::FDisplayClusterConfigurationICVFX_StageSettings()
	: DefaultFrameSize(2560, 1440)
{
}

UDisplayClusterConfigurationViewport::UDisplayClusterConfigurationViewport()
{
#if WITH_EDITORONLY_DATA
	bIsVisible = true;
	bIsEnabled = true;
#endif
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

void UDisplayClusterConfigurationHostDisplayData::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
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

UDisplayClusterConfigurationClusterNode::UDisplayClusterConfigurationClusterNode()
{
	const FDisplayClusterConfigurationTextClusterNode DefaultValues;
	
	bIsSoundEnabled = DefaultValues.SoundEnabled;

#if WITH_EDITORONLY_DATA
	bIsVisible = true;
	bIsEnabled = true;
#endif
}

UDisplayClusterConfigurationHostDisplayData::UDisplayClusterConfigurationHostDisplayData()
{
	bIsVisible = true;
	bIsEnabled = true;
	SetFlags(RF_Public);
}

void UDisplayClusterConfigurationClusterNode::GetObjectsToExport(TArray<UObject*>& OutObjects)
{
	Super::GetObjectsToExport(OutObjects);
	SAVE_MAP(Viewports);
}

void UDisplayClusterConfigurationCluster::GetObjectsToExport(TArray<UObject*>& OutObjects)
{
	Super::GetObjectsToExport(OutObjects);
	SAVE_MAP(Nodes);
}

void UDisplayClusterConfigurationData_Base::Serialize(FArchive& Ar)
{
#if WITH_EDITOR
	if (Ar.IsSaving())
	{
		/*
		 * We need to set everything to public so it can be saved & referenced properly with the object.
		 * The object ownership doesn't seem to be correct at this stage either and is sometimes
		 * owned by the main data object, but since subobjects embed subobjects the correct parent
		 * should be set prior to save.
		 */

		SetFlags(RF_Public);

		ExportedObjects.Reset();
		GetObjectsToExport(ExportedObjects);
		for (UObject* Object : ExportedObjects)
		{
			if (!ensure(Object != nullptr))
			{
				UE_LOG(LogDisplayClusterConfiguration, Warning, TEXT("Null object passed to GetObjectsToExport"));
				continue;
			}
			if (Object->GetOuter() != this)
			{
				Object->Rename(nullptr, this, REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors);
			}
			Object->SetFlags(RF_Public);
		}
	}
#endif
	
	Super::Serialize(Ar);
}

void UDisplayClusterConfigurationScene::GetObjectsToExport(TArray<UObject*>& OutObjects)
{
	Super::GetObjectsToExport(OutObjects);
	
	SAVE_MAP(Xforms);
	SAVE_MAP(Screens);
	SAVE_MAP(Cameras);
	SAVE_MAP(Meshes);
}

FDisplayClusterConfigurationMasterNodePorts::FDisplayClusterConfigurationMasterNodePorts()
{
	const FDisplayClusterConfigurationTextClusterNode DefaultValues;
	
	ClusterSync = DefaultValues.Port_CS;
	RenderSync = DefaultValues.Port_SS;
	ClusterEventsJson = DefaultValues.Port_CE;
	ClusterEventsBinary = DefaultValues.Port_CEB;
}

FDisplayClusterConfigurationClusterSync::FDisplayClusterConfigurationClusterSync()
{
	using namespace DisplayClusterConfigurationStrings::config;
	RenderSyncPolicy.Type = cluster::render_sync::Ethernet;
	InputSyncPolicy.Type = cluster::input_sync::InputSyncPolicyReplicateMaster;
}

FDisplayClusterConfigurationNetworkSettings::FDisplayClusterConfigurationNetworkSettings()
{
	const FDisplayClusterConfigurationTextNetwork DefaultValues;
	
	ConnectRetriesAmount = DefaultValues.ClientConnectTriesAmount;
	ConnectRetryDelay = DefaultValues.ClientConnectRetryDelay;
	GameStartBarrierTimeout = DefaultValues.BarrierGameStartWaitTimeout;
	FrameStartBarrierTimeout = FrameEndBarrierTimeout = RenderSyncBarrierTimeout = DefaultValues.BarrierWaitTimeout;
}

void UDisplayClusterConfigurationViewport::GetReferencedMeshNames(TArray<FString>& OutMeshNames) const
{
	// Collect all mesh references from projection policies
	for (const TPair<FString, FString>& It : ProjectionPolicy.Parameters)
	{
		if (ProjectionPolicy.Type.Compare(DisplayClusterProjectionStrings::projection::Mesh, ESearchCase::IgnoreCase) == 0)
		{
			if (It.Key.Compare(DisplayClusterProjectionStrings::cfg::mesh::Component, ESearchCase::IgnoreCase) == 0)
			{
				OutMeshNames.Add(It.Value);
			}
		}
	}
}

void UDisplayClusterConfigurationClusterNode::GetReferencedMeshNames(TArray<FString>& OutMeshNames) const
{
	for (const TPair<FString, UDisplayClusterConfigurationViewport*>& It : Viewports)
	{
		if (It.Value == nullptr)
		{
			// Could be null temporarily during a reimport when this is called.
			continue;
		}
		It.Value->GetReferencedMeshNames(OutMeshNames);
	}
}

void UDisplayClusterConfigurationCluster::GetReferencedMeshNames(TArray<FString>& OutMeshNames) const
{
	for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& It : Nodes)
	{
		if (It.Value == nullptr)
		{
			// Could be null temporarily during a reimport when this is called.
			continue;
		}
		It.Value->GetReferencedMeshNames(OutMeshNames);
	}
}

void UDisplayClusterConfigurationData::GetReferencedMeshNames(TArray<FString>& OutMeshNames) const
{
	if (Cluster != nullptr)
	{
		Cluster->GetReferencedMeshNames(OutMeshNames);
	}
}

UDisplayClusterConfigurationData* UDisplayClusterConfigurationData::CreateNewConfigData(UObject* Owner, EObjectFlags ObjectFlags)
{
	UDisplayClusterConfigurationData* NewConfigData = NewObject<UDisplayClusterConfigurationData>(Owner ? Owner : GetTransientPackage(), NAME_None,
		ObjectFlags | RF_ArchetypeObject | RF_Public | RF_Transactional);
	
	NewConfigData->Cluster = NewObject<UDisplayClusterConfigurationCluster>(NewConfigData, NAME_None,
		RF_ArchetypeObject | RF_Public | RF_Transactional);

	return NewConfigData;
}
