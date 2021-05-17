// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/JSON/DisplayClusterConfigurationJsonParser.h"

#include "Formats/JSON/DisplayClusterConfigurationJsonHelpers.h"
#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterConfigurationLog.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfigurationStrings.h"

#include "JsonObjectConverter.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


UDisplayClusterConfigurationData* FDisplayClusterConfigurationJsonParser::LoadData(const FString& FilePath, UObject* Owner)
{
	ConfigDataOwner = Owner;

	FString JsonText;

	// Load json text to the string object
	if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Couldn't read file: %s"), *FilePath);
		return nullptr;
	}

	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("nDisplay json configuration: %s"), *JsonText);

	// Parse the string object
	if (!FJsonObjectConverter::JsonObjectStringToUStruct<FDisplayClusterConfigurationJsonContainer>(JsonText, &JsonData, 0, 0))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Couldn't deserialize json file: %s"), *FilePath);
		return nullptr;
	}

	ConfigFile = FilePath;

	// Finally, convert the data to nDisplay internal types
	return ConvertDataToInternalTypes();
}

bool FDisplayClusterConfigurationJsonParser::SaveData(const UDisplayClusterConfigurationData* ConfigData, const FString& FilePath)
{
	FString JsonTextOut;

	// Convert to json string
	if (!AsString(ConfigData, JsonTextOut))
	{
		return false;
	}

	// Save json string to a file
	if (!FFileHelper::SaveStringToFile(JsonTextOut, *FilePath))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Couldn't save data to file: %s"), *FilePath);
		return false;
	}

	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Configuration data has been successfully saved to file: %s"), *FilePath);

	return true;
}

bool FDisplayClusterConfigurationJsonParser::AsString(const UDisplayClusterConfigurationData* ConfigData, FString& OutString)
{
	// Convert nDisplay internal types to json types
	if (!ConvertDataToExternalTypes(ConfigData))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Couldn't convert data to json data types"));
		return false;
	}

	// Serialize json types to json string
	if (!FJsonObjectConverter::UStructToJsonObjectString<FDisplayClusterConfigurationJsonContainer>(JsonData, OutString))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Couldn't serialize data to json"));
		return false;
	}

	return true;
}

UDisplayClusterConfigurationData* FDisplayClusterConfigurationJsonParser::ConvertDataToInternalTypes()
{
	UDisplayClusterConfigurationData* Config = UDisplayClusterConfigurationData::CreateNewConfigData(ConfigDataOwner, RF_MarkAsRootSet);
	check(Config && Config->Scene && Config->Cluster);

	// Fill metadata
	Config->Meta.DataSource = EDisplayClusterConfigurationDataSource::Json;
	Config->Meta.FilePath   = ConfigFile;

	const FDisplayClusterConfigurationJsonNdisplay& CfgJson = JsonData.nDisplay;

	// Info
	Config->Info.Version     = CfgJson.Version;
	Config->Info.Description = CfgJson.Description;

	// Scene
	{
		// Xforms
		for (const auto& CfgComp : CfgJson.Scene.Xforms)
		{
			UDisplayClusterConfigurationSceneComponentXform* Comp = NewObject<UDisplayClusterConfigurationSceneComponentXform>(Config, NAME_None, RF_Transactional);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.Value.Parent;
			Comp->Location       = FDisplayClusterConfigurationJsonVector::ToVector(CfgComp.Value.Location) * 100.f;
			Comp->Rotation       = FDisplayClusterConfigurationJsonRotator::ToRotator(CfgComp.Value.Rotation);

			Config->Scene->Xforms.Emplace(CfgComp.Key, Comp);
		}

		// Screens
		for (const auto& CfgComp : CfgJson.Scene.Screens)
		{
			UDisplayClusterConfigurationSceneComponentScreen* Comp = NewObject<UDisplayClusterConfigurationSceneComponentScreen>(Config, NAME_None, RF_Transactional);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.Value.Parent;
			Comp->Location       = FDisplayClusterConfigurationJsonVector::ToVector(CfgComp.Value.Location) * 100.f;
			Comp->Rotation       = FDisplayClusterConfigurationJsonRotator::ToRotator(CfgComp.Value.Rotation);
			// Screen specific
			Comp->Size = FDisplayClusterConfigurationJsonSizeFloat::ToVector(CfgComp.Value.Size);

			Config->Scene->Screens.Emplace(CfgComp.Key, Comp);
		}

		// Cameras
		for (const auto& CfgComp : CfgJson.Scene.Cameras)
		{
			UDisplayClusterConfigurationSceneComponentCamera* Comp = NewObject<UDisplayClusterConfigurationSceneComponentCamera>(Config, NAME_None, RF_Transactional);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.Value.Parent;
			Comp->Location       = FDisplayClusterConfigurationJsonVector::ToVector(CfgComp.Value.Location) * 100.f;
			Comp->Rotation       = FDisplayClusterConfigurationJsonRotator::ToRotator(CfgComp.Value.Rotation);
			// Camera specific
			Comp->InterpupillaryDistance = CfgComp.Value.InterpupillaryDistance;
			Comp->bSwapEyes      = CfgComp.Value.SwapEyes;
			Comp->StereoOffset   = DisplayClusterConfigurationJsonHelpers::FromString<EDisplayClusterConfigurationEyeStereoOffset>(CfgComp.Value.StereoOffset);

			Config->Scene->Cameras.Emplace(CfgComp.Key, Comp);
		}

		// Meshes
		for (const auto& CfgComp : CfgJson.Scene.Meshes)
		{
			UDisplayClusterConfigurationSceneComponentMesh* Comp = NewObject<UDisplayClusterConfigurationSceneComponentMesh>(Config, NAME_None, RF_Transactional);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.Value.Parent;
			Comp->Location       = FDisplayClusterConfigurationJsonVector::ToVector(CfgComp.Value.Location) * 100.f;
			Comp->Rotation       = FDisplayClusterConfigurationJsonRotator::ToRotator(CfgComp.Value.Rotation);
			// Mesh specific
			Comp->AssetPath      = CfgComp.Value.Asset;

			Config->Scene->Meshes.Emplace(CfgComp.Key, Comp);
		}
	}

	// Cluster
	{
		// Master node
		{
			Config->Cluster->MasterNode.Id = CfgJson.Cluster.MasterNode.Id;
			
			const uint16* ClusterSyncPort = CfgJson.Cluster.MasterNode.Ports.Find(DisplayClusterConfigurationStrings::config::cluster::ports::PortClusterSync);
			Config->Cluster->MasterNode.Ports.ClusterSync = (ClusterSyncPort ? *ClusterSyncPort : 41001);

			const uint16* RenderSyncPort = CfgJson.Cluster.MasterNode.Ports.Find(DisplayClusterConfigurationStrings::config::cluster::ports::PortRenderSync);
			Config->Cluster->MasterNode.Ports.RenderSync = (RenderSyncPort ? *RenderSyncPort : 41002);

			const uint16* ClusterEventsJsonPort = CfgJson.Cluster.MasterNode.Ports.Find(DisplayClusterConfigurationStrings::config::cluster::ports::PortClusterEventsJson);
			Config->Cluster->MasterNode.Ports.ClusterEventsJson = (ClusterEventsJsonPort ? *ClusterEventsJsonPort : 41003);

			const uint16* ClusterEventsBinaryPort = CfgJson.Cluster.MasterNode.Ports.Find(DisplayClusterConfigurationStrings::config::cluster::ports::PortClusterEventsBinary);
			Config->Cluster->MasterNode.Ports.ClusterEventsBinary = (ClusterEventsBinaryPort ? *ClusterEventsBinaryPort : 41004);
		}

		// Cluster sync
		{
			// Native input sync
			Config->Cluster->Sync.InputSyncPolicy.Type        = CfgJson.Cluster.Sync.InputSyncPolicy.Type;
			Config->Cluster->Sync.InputSyncPolicy.Parameters  = CfgJson.Cluster.Sync.InputSyncPolicy.Parameters;

			// Render sync
			Config->Cluster->Sync.RenderSyncPolicy.Type       = CfgJson.Cluster.Sync.RenderSyncPolicy.Type;
			Config->Cluster->Sync.RenderSyncPolicy.Parameters = CfgJson.Cluster.Sync.RenderSyncPolicy.Parameters;
		}

		// Network
		{
			Config->Cluster->Network.ConnectRetriesAmount     = DisplayClusterHelpers::map::template ExtractValueFromString(
				CfgJson.Cluster.Network, FString(DisplayClusterConfigurationStrings::config::cluster::network::NetConnectRetriesAmount),     (uint32)15);

			Config->Cluster->Network.ConnectRetryDelay        = DisplayClusterHelpers::map::template ExtractValueFromString(
				CfgJson.Cluster.Network, FString(DisplayClusterConfigurationStrings::config::cluster::network::NetConnectRetryDelay),        (uint32)1000);

			Config->Cluster->Network.GameStartBarrierTimeout  = DisplayClusterHelpers::map::template ExtractValueFromString(
				CfgJson.Cluster.Network, FString(DisplayClusterConfigurationStrings::config::cluster::network::NetGameStartBarrierTimeout),  (uint32)30000);

			Config->Cluster->Network.FrameStartBarrierTimeout = DisplayClusterHelpers::map::template ExtractValueFromString(
				CfgJson.Cluster.Network, FString(DisplayClusterConfigurationStrings::config::cluster::network::NetFrameStartBarrierTimeout), (uint32)5000);

			Config->Cluster->Network.FrameEndBarrierTimeout   = DisplayClusterHelpers::map::template ExtractValueFromString(
				CfgJson.Cluster.Network, FString(DisplayClusterConfigurationStrings::config::cluster::network::NetFrameEndBarrierTimeout),   (uint32)5000);

			Config->Cluster->Network.RenderSyncBarrierTimeout   = DisplayClusterHelpers::map::template ExtractValueFromString(
				CfgJson.Cluster.Network, FString(DisplayClusterConfigurationStrings::config::cluster::network::NetRenderSyncBarrierTimeout),   (uint32)5000);
		}

		// Cluster nodes
		for (const auto& CfgNode : CfgJson.Cluster.Nodes)
		{
			UDisplayClusterConfigurationClusterNode* Node = NewObject<UDisplayClusterConfigurationClusterNode>(Config->Cluster, *CfgNode.Key, RF_Transactional);
			check(Node);

			// Base parameters
			Node->Host            = CfgNode.Value.Host;
			Node->bIsSoundEnabled = CfgNode.Value.Sound;
			Node->bIsFullscreen   = CfgNode.Value.FullScreen;
			Node->WindowRect      = FDisplayClusterConfigurationRectangle(CfgNode.Value.Window.X, CfgNode.Value.Window.Y, CfgNode.Value.Window.W, CfgNode.Value.Window.H);

			// Viewports
			for (const auto& CfgViewport : CfgNode.Value.Viewports)
			{
				UDisplayClusterConfigurationViewport* Viewport = NewObject<UDisplayClusterConfigurationViewport>(Node, *CfgViewport.Key, RF_Transactional | RF_ArchetypeObject | RF_Public);
				check(Viewport);

				// Base parameters
				Viewport->RenderSettings.BufferRatio = CfgViewport.Value.BufferRatio;
				Viewport->Camera      = CfgViewport.Value.Camera;
				Viewport->Region      = FDisplayClusterConfigurationRectangle(CfgViewport.Value.Region.X, CfgViewport.Value.Region.Y, CfgViewport.Value.Region.W, CfgViewport.Value.Region.H);
				Viewport->GPUIndex    = CfgViewport.Value.GPUIndex;
				Viewport->bIsShared   = CfgViewport.Value.IsShared;
				//Viewport->bAllowCrossGPUTransfer = CfgViewport.Value.AllowCrossGPUTransfer;

				// Projection policy
				Viewport->ProjectionPolicy.Type       = CfgViewport.Value.ProjectionPolicy.Type;
				Viewport->ProjectionPolicy.Parameters = CfgViewport.Value.ProjectionPolicy.Parameters;

				// Add this viewport
				Node->Viewports.Emplace(CfgViewport.Key, Viewport);
			}

			// Postprocess
			for (const auto& CfgPostprocess : CfgNode.Value.Postprocess)
			{
				FDisplayClusterConfigurationPostprocess PostprocessOperation;

				PostprocessOperation.Type       = CfgPostprocess.Value.Type;
				PostprocessOperation.Parameters = CfgPostprocess.Value.Parameters;
				
				Node->Postprocess.Emplace(CfgPostprocess.Key, PostprocessOperation);
			}

			// Store new cluster node
			Config->Cluster->Nodes.Emplace(CfgNode.Key, Node);
		}
	}

	// Custom parameters
	Config->CustomParameters = CfgJson.CustomParameters;

	// Diagnostics
	Config->Diagnostics.bSimulateLag = CfgJson.Diagnostics.SimulateLag;
	Config->Diagnostics.MinLagTime   = CfgJson.Diagnostics.MinLagTime;
	Config->Diagnostics.MaxLagTime   = CfgJson.Diagnostics.MaxLagTime;

	return Config;
}

bool FDisplayClusterConfigurationJsonParser::ConvertDataToExternalTypes(const UDisplayClusterConfigurationData* Config)
{
	if (!(Config && Config->Cluster))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("nullptr detected in the configuration data"));
		return false;
	}

	FDisplayClusterConfigurationJsonNdisplay& Json = JsonData.nDisplay;

	// Info
	Json.Description = Config->Info.Description;
	Json.Version     = Config->Info.Version;

	// Scene
	if(Config->Scene)
	{
		// Xforms
		for (const auto& CfgComp : Config->Scene->Xforms)
		{
			FDisplayClusterConfigurationJsonSceneComponentXform Xform;

			// General
			Xform.Parent   = CfgComp.Value->ParentId;
			Xform.Location = FDisplayClusterConfigurationJsonVector::FromVector(CfgComp.Value->Location / 100.f);
			Xform.Rotation = FDisplayClusterConfigurationJsonRotator::FromRotator(CfgComp.Value->Rotation);

			Json.Scene.Xforms.Emplace(CfgComp.Key, Xform);
		}

		// Screens
		for (const auto& CfgComp : Config->Scene->Screens)
		{
			FDisplayClusterConfigurationJsonSceneComponentScreen Screen;

			// General
			Screen.Parent   = CfgComp.Value->ParentId;
			Screen.Location = FDisplayClusterConfigurationJsonVector::FromVector(CfgComp.Value->Location);
			Screen.Rotation = FDisplayClusterConfigurationJsonRotator::FromRotator(CfgComp.Value->Rotation);
			// Screen specific
			Screen.Size = FDisplayClusterConfigurationJsonSizeFloat::FromVector(CfgComp.Value->Size);

			Json.Scene.Screens.Emplace(CfgComp.Key, Screen);
		}

		// Cameras
		for (const auto& CfgComp : Config->Scene->Cameras)
		{
			FDisplayClusterConfigurationJsonSceneComponentCamera Camera;

			// General
			Camera.Parent   = CfgComp.Value->ParentId;
			Camera.Location = FDisplayClusterConfigurationJsonVector::FromVector(CfgComp.Value->Location / 100.f);
			Camera.Rotation = FDisplayClusterConfigurationJsonRotator::FromRotator(CfgComp.Value->Rotation);
			// Camera specific
			Camera.InterpupillaryDistance = CfgComp.Value->InterpupillaryDistance;
			Camera.SwapEyes = CfgComp.Value->bSwapEyes;
			Camera.StereoOffset = DisplayClusterConfigurationJsonHelpers::ToString(CfgComp.Value->StereoOffset);

			Json.Scene.Cameras.Emplace(CfgComp.Key, Camera);
		}

		// Meshes
		for (const auto& CfgComp : Config->Scene->Meshes)
		{
			FDisplayClusterConfigurationJsonSceneComponentMesh Mesh;

			// General
			Mesh.Parent    = CfgComp.Value->ParentId;
			Mesh.Location  = FDisplayClusterConfigurationJsonVector::FromVector(CfgComp.Value->Location / 100.f);
			Mesh.Rotation  = FDisplayClusterConfigurationJsonRotator::FromRotator(CfgComp.Value->Rotation);
			// Mesh specific
			Mesh.Asset = CfgComp.Value->AssetPath;

			Json.Scene.Meshes.Emplace(CfgComp.Key, Mesh);
		}
	}

	// Cluster
	{
		// Master node
		{
			Json.Cluster.MasterNode.Id = Config->Cluster->MasterNode.Id;
			Json.Cluster.MasterNode.Ports.Emplace(DisplayClusterConfigurationStrings::config::cluster::ports::PortClusterSync, Config->Cluster->MasterNode.Ports.ClusterSync);
			Json.Cluster.MasterNode.Ports.Emplace(DisplayClusterConfigurationStrings::config::cluster::ports::PortRenderSync, Config->Cluster->MasterNode.Ports.RenderSync);
			Json.Cluster.MasterNode.Ports.Emplace(DisplayClusterConfigurationStrings::config::cluster::ports::PortClusterEventsJson, Config->Cluster->MasterNode.Ports.ClusterEventsJson);
			Json.Cluster.MasterNode.Ports.Emplace(DisplayClusterConfigurationStrings::config::cluster::ports::PortClusterEventsBinary, Config->Cluster->MasterNode.Ports.ClusterEventsBinary);
		}

		// Cluster sync
		{
			// Native input sync
			Json.Cluster.Sync.InputSyncPolicy.Type        = Config->Cluster->Sync.InputSyncPolicy.Type;
			Json.Cluster.Sync.InputSyncPolicy.Parameters  = Config->Cluster->Sync.InputSyncPolicy.Parameters;

			// Render sync
			Json.Cluster.Sync.RenderSyncPolicy.Type       = Config->Cluster->Sync.RenderSyncPolicy.Type;
			Json.Cluster.Sync.RenderSyncPolicy.Parameters = Config->Cluster->Sync.RenderSyncPolicy.Parameters;
		}

		// Network
		{
			Json.Cluster.Network.Emplace(DisplayClusterConfigurationStrings::config::cluster::network::NetConnectRetriesAmount,
				DisplayClusterTypesConverter::template ToString(Config->Cluster->Network.ConnectRetriesAmount));

			Json.Cluster.Network.Emplace(DisplayClusterConfigurationStrings::config::cluster::network::NetConnectRetryDelay,
				DisplayClusterTypesConverter::template ToString(Config->Cluster->Network.ConnectRetryDelay));

			Json.Cluster.Network.Emplace(DisplayClusterConfigurationStrings::config::cluster::network::NetGameStartBarrierTimeout,
				DisplayClusterTypesConverter::template ToString(Config->Cluster->Network.GameStartBarrierTimeout));

			Json.Cluster.Network.Emplace(DisplayClusterConfigurationStrings::config::cluster::network::NetFrameStartBarrierTimeout,
				DisplayClusterTypesConverter::template ToString(Config->Cluster->Network.FrameStartBarrierTimeout));

			Json.Cluster.Network.Emplace(DisplayClusterConfigurationStrings::config::cluster::network::NetFrameEndBarrierTimeout,
				DisplayClusterTypesConverter::template ToString(Config->Cluster->Network.FrameEndBarrierTimeout));

			Json.Cluster.Network.Emplace(DisplayClusterConfigurationStrings::config::cluster::network::NetRenderSyncBarrierTimeout,
				DisplayClusterTypesConverter::template ToString(Config->Cluster->Network.RenderSyncBarrierTimeout));
		}

		// Cluster nodes
		for (const auto& CfgNode : Config->Cluster->Nodes)
		{
			FDisplayClusterConfigurationJsonClusterNode Node;

			// Base parameters
			Node.Host       = CfgNode.Value->Host;
			Node.Sound      = CfgNode.Value->bIsSoundEnabled;
			Node.FullScreen = CfgNode.Value->bIsFullscreen;
			Node.Window     = FDisplayClusterConfigurationJsonRectangle(CfgNode.Value->WindowRect.X, CfgNode.Value->WindowRect.Y, CfgNode.Value->WindowRect.W, CfgNode.Value->WindowRect.H);

			// Viewports
			for (const auto& CfgViewport : CfgNode.Value->Viewports)
			{
				FDisplayClusterConfigurationJsonViewport Viewport;

				// Base parameters
				Viewport.Camera   = CfgViewport.Value->Camera;
				Viewport.Region   = FDisplayClusterConfigurationJsonRectangle(CfgViewport.Value->Region.X, CfgViewport.Value->Region.Y, CfgViewport.Value->Region.W, CfgViewport.Value->Region.H);
				Viewport.GPUIndex = CfgViewport.Value->GPUIndex;
				Viewport.IsShared = CfgViewport.Value->bIsShared;
				Viewport.BufferRatio = CfgViewport.Value->RenderSettings.BufferRatio;
				//Viewport.AllowCrossGPUTransfer = CfgViewport.Value->bAllowCrossGPUTransfer;

				// Projection policy
				Viewport.ProjectionPolicy.Type = CfgViewport.Value->ProjectionPolicy.Type;
				Viewport.ProjectionPolicy.Parameters = CfgViewport.Value->ProjectionPolicy.Parameters;

				// Save this viewport
				Node.Viewports.Emplace(CfgViewport.Key, Viewport);
			}

			// Postprocess
			for (const auto& CfgPostprocess : CfgNode.Value->Postprocess)
			{
				FDisplayClusterConfigurationJsonPostprocess PostprocessOperation;

				PostprocessOperation.Type       = CfgPostprocess.Value.Type;
				PostprocessOperation.Parameters = CfgPostprocess.Value.Parameters;
				
				Node.Postprocess.Emplace(CfgPostprocess.Key, PostprocessOperation);
			}

			// Store new cluster node
			Json.Cluster.Nodes.Emplace(CfgNode.Key, Node);
		}
	}

	// Custom parameters
	Json.CustomParameters = Config->CustomParameters;

	// Diagnostics
	Json.Diagnostics.SimulateLag = Config->Diagnostics.bSimulateLag;
	Json.Diagnostics.MinLagTime  = Config->Diagnostics.MinLagTime;
	Json.Diagnostics.MaxLagTime  = Config->Diagnostics.MaxLagTime;

	return true;
}
