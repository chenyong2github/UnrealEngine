// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/JSON/DisplayClusterConfigurationJsonParser.h"

#include "Formats/JSON/DisplayClusterConfigurationJsonHelpers.h"
#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterConfigurationLog.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterCOnfigurationStrings.h"

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
	// Convert nDisplay internal types to json types
	if (!ConvertDataToExternalTypes(ConfigData))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Couldn't convert data to json data types"));
		return false;
	}

	// Serialize json types to json string
	FString JsonTextOut;
	if (!FJsonObjectConverter::UStructToJsonObjectString<FDisplayClusterConfigurationJsonContainer>(JsonData, JsonTextOut))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Couldn't serialize data to json"));
		return false;
	}

	// Finally, save json text to a file
	if (!FFileHelper::SaveStringToFile(JsonTextOut, *FilePath))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Couldn't save data to file: %s"), *FilePath);
		return false;
	}

	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Configuration data has been successfully saved to file: %s"), *FilePath);

	return true;
}

UDisplayClusterConfigurationData* FDisplayClusterConfigurationJsonParser::ConvertDataToInternalTypes()
{
	UDisplayClusterConfigurationData* Config = NewObject<UDisplayClusterConfigurationData>(ConfigDataOwner ? ConfigDataOwner : GetTransientPackage(), NAME_None, RF_MarkAsRootSet);
	check(Config && Config->Scene && Config->Input && Config->Cluster);

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
			UDisplayClusterConfigurationSceneComponentXform* Comp = NewObject<UDisplayClusterConfigurationSceneComponentXform>(Config);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.Value.Parent;
			Comp->Location       = FDisplayClusterConfigurationJsonVector::ToVector(CfgComp.Value.Location) * 100.f;
			Comp->Rotation       = FDisplayClusterConfigurationJsonRotator::ToRotator(CfgComp.Value.Rotation);
			Comp->TrackerId      = CfgComp.Value.TrackerId;
			Comp->TrackerChannel = CfgComp.Value.TrackerChannel;

			Config->Scene->Xforms.Emplace(CfgComp.Key, Comp);
		}

		// Screens
		for (const auto& CfgComp : CfgJson.Scene.Screens)
		{
			UDisplayClusterConfigurationSceneComponentScreen* Comp = NewObject<UDisplayClusterConfigurationSceneComponentScreen>(Config);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.Value.Parent;
			Comp->Location       = FDisplayClusterConfigurationJsonVector::ToVector(CfgComp.Value.Location) * 100.f;
			Comp->Rotation       = FDisplayClusterConfigurationJsonRotator::ToRotator(CfgComp.Value.Rotation);
			Comp->TrackerId      = CfgComp.Value.TrackerId;
			Comp->TrackerChannel = CfgComp.Value.TrackerChannel;
			// Screen specific
			Comp->Size = FDisplayClusterConfigurationJsonSizeFloat::ToVector(CfgComp.Value.Size) * 100;

			Config->Scene->Screens.Emplace(CfgComp.Key, Comp);
		}

		// Cameras
		for (const auto& CfgComp : CfgJson.Scene.Cameras)
		{
			UDisplayClusterConfigurationSceneComponentCamera* Comp = NewObject<UDisplayClusterConfigurationSceneComponentCamera>(Config);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.Value.Parent;
			Comp->Location       = FDisplayClusterConfigurationJsonVector::ToVector(CfgComp.Value.Location) * 100.f;
			Comp->Rotation       = FDisplayClusterConfigurationJsonRotator::ToRotator(CfgComp.Value.Rotation);
			Comp->TrackerId      = CfgComp.Value.TrackerId;
			Comp->TrackerChannel = CfgComp.Value.TrackerChannel;
			// Camera specific
			Comp->InterpupillaryDistance = CfgComp.Value.InterpupillaryDistance * 100.f;
			Comp->bSwapEyes      = CfgComp.Value.SwapEyes;
			Comp->StereoOffset   = DisplayClusterConfigurationJsonHelpers::FromString<EDisplayClusterConfigurationEyeStereoOffset>(CfgComp.Value.StereoOffset);

			Config->Scene->Cameras.Emplace(CfgComp.Key, Comp);
		}

		// Meshes
		for (const auto& CfgComp : CfgJson.Scene.Meshes)
		{
			UDisplayClusterConfigurationSceneComponentMesh* Comp = NewObject<UDisplayClusterConfigurationSceneComponentMesh>(Config);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.Value.Parent;
			Comp->Location       = FDisplayClusterConfigurationJsonVector::ToVector(CfgComp.Value.Location) * 100.f;
			Comp->Rotation       = FDisplayClusterConfigurationJsonRotator::ToRotator(CfgComp.Value.Rotation);
			Comp->TrackerId      = CfgComp.Value.TrackerId;
			Comp->TrackerChannel = CfgComp.Value.TrackerChannel;
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
			UDisplayClusterConfigurationClusterNode* Node = NewObject<UDisplayClusterConfigurationClusterNode>(Config);
			check(Node);

			// Base parameters
			Node->Host            = CfgNode.Value.Host;
			Node->bIsSoundEnabled = CfgNode.Value.Sound;
			Node->bIsFullscreen   = CfgNode.Value.FullScreen;
			Node->WindowRect      = FDisplayClusterConfigurationRectangle(CfgNode.Value.Window.X, CfgNode.Value.Window.Y, CfgNode.Value.Window.W, CfgNode.Value.Window.H);

			// Viewports
			for (const auto& CfgViewport : CfgNode.Value.Viewports)
			{
				UDisplayClusterConfigurationViewport* Viewport = NewObject<UDisplayClusterConfigurationViewport>(Config);
				check(Viewport);

				// Base parameters
				Viewport->BufferRatio = CfgViewport.Value.BufferRatio;
				Viewport->Camera      = CfgViewport.Value.Camera;
				Viewport->Region      = FDisplayClusterConfigurationRectangle(CfgViewport.Value.Region.X, CfgViewport.Value.Region.Y, CfgViewport.Value.Region.W, CfgViewport.Value.Region.H);
				Viewport->GPUIndex    = CfgViewport.Value.GPUIndex;
				Viewport->bIsShared   = CfgViewport.Value.IsShared;
				Viewport->bAllowCrossGPUTransfer = CfgViewport.Value.AllowCrossGPUTransfer;

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

	for (const auto& CfgInput : CfgJson.Input)
	{
		// Common parameter - address
		FString Address;
		DisplayClusterHelpers::map::ExtractValue(CfgInput.Value.Parameters, DisplayClusterConfigurationStrings::config::input::devices::Address, Address);

		// Common parameter - channel mapping
		TMap<int32, int32> ChannelRemapping;
		DisplayClusterHelpers::map::ExtractMapFromString(CfgInput.Value.Parameters, DisplayClusterConfigurationStrings::config::input::devices::Remapping, ChannelRemapping, FString(","), FString(":"));

		if (CfgInput.Value.Type.Equals(DisplayClusterConfigurationStrings::config::input::devices::VrpnDeviceAnalog, ESearchCase::IgnoreCase))
		{
			UDisplayClusterConfigurationInputDeviceAnalog* InputDevice = NewObject<UDisplayClusterConfigurationInputDeviceAnalog>(Config);
			check(InputDevice);

			InputDevice->Address = Address;
			InputDevice->ChannelRemapping = ChannelRemapping;

			Config->Input->AnalogDevices.Emplace(CfgInput.Key, InputDevice);

		}
		else if (CfgInput.Value.Type.Equals(DisplayClusterConfigurationStrings::config::input::devices::VrpnDeviceButton, ESearchCase::IgnoreCase))
		{
			UDisplayClusterConfigurationInputDeviceButton* InputDevice = NewObject<UDisplayClusterConfigurationInputDeviceButton>(Config);
			check(InputDevice);

			InputDevice->Address = Address;
			InputDevice->ChannelRemapping = ChannelRemapping;

			Config->Input->ButtonDevices.Emplace(CfgInput.Key, InputDevice);
		}
		else if (CfgInput.Value.Type.Equals(DisplayClusterConfigurationStrings::config::input::devices::VrpnDeviceKeyboard, ESearchCase::IgnoreCase))
		{
			UDisplayClusterConfigurationInputDeviceKeyboard* InputDevice = NewObject<UDisplayClusterConfigurationInputDeviceKeyboard>(Config);
			check(InputDevice);

			InputDevice->Address = Address;
			InputDevice->ChannelRemapping = ChannelRemapping;
			InputDevice->ReflectionType = EDisplayClusterConfigurationKeyboardReflectionType::None;

			FString ReflectionType;
			DisplayClusterHelpers::map::ExtractValue(CfgInput.Value.Parameters, DisplayClusterConfigurationStrings::config::input::devices::ReflectType, ReflectionType);
			InputDevice->ReflectionType = DisplayClusterConfigurationJsonHelpers::FromString<EDisplayClusterConfigurationKeyboardReflectionType>(ReflectionType);

			Config->Input->KeyboardDevices.Emplace(CfgInput.Key, InputDevice);
		}
		else if (CfgInput.Value.Type.Equals(DisplayClusterConfigurationStrings::config::input::devices::VrpnDeviceTracker, ESearchCase::IgnoreCase))
		{
			UDisplayClusterConfigurationInputDeviceTracker* InputDevice = NewObject<UDisplayClusterConfigurationInputDeviceTracker>(Config);
			check(InputDevice);

			InputDevice->Address = Address;
			InputDevice->ChannelRemapping = ChannelRemapping;

			TArray<float> Location;
			if (DisplayClusterHelpers::map::ExtractArrayFromString(CfgInput.Value.Parameters, DisplayClusterConfigurationStrings::config::input::devices::OriginLocation, Location))
			{
				switch (Location.Num())
				{
				case 0:
					// Nothing to do, a default location will be set
					break;

				case 3:
					InputDevice->OriginLocation = FVector(Location[0], Location[1], Location[2]) * 100.f;
					break;

				default:
					UE_LOG(LogDisplayClusterConfiguration, Warning, TEXT("Wrong location data for tracker '%s'. Default zero location will be used."), *CfgInput.Key);
					break;
				}
			}

			TArray<float> Rotation;
			if (DisplayClusterHelpers::map::ExtractArrayFromString(CfgInput.Value.Parameters, DisplayClusterConfigurationStrings::config::input::devices::OriginRotation, Rotation))
			{
				switch (Rotation.Num())
				{
				case 0:
					// Nothing to do, a default location will be set
					break;

				case 3:
					InputDevice->OriginRotation = FRotator(Rotation[0], Rotation[1], Rotation[2]);
					break;

				default:
					UE_LOG(LogDisplayClusterConfiguration, Warning, TEXT("Wrong rotation data for tracker '%s'. Default zero rotation will be used."), *CfgInput.Key);
					break;
				}
			}

			DisplayClusterHelpers::map::ExtractValueFromString(CfgInput.Value.Parameters, DisplayClusterConfigurationStrings::config::input::devices::OriginComponent, InputDevice->OriginComponent);

			FString Front;
			DisplayClusterHelpers::map::ExtractValue(CfgInput.Value.Parameters, DisplayClusterConfigurationStrings::config::input::devices::Front, Front);
			InputDevice->Front = DisplayClusterConfigurationJsonHelpers::FromString<EDisplayClusterConfigurationTrackerMapping>(Front);

			FString Right;
			DisplayClusterHelpers::map::ExtractValue(CfgInput.Value.Parameters, DisplayClusterConfigurationStrings::config::input::devices::Right, Right);
			InputDevice->Right = DisplayClusterConfigurationJsonHelpers::FromString<EDisplayClusterConfigurationTrackerMapping>(Right);

			FString Up;
			DisplayClusterHelpers::map::ExtractValue(CfgInput.Value.Parameters, DisplayClusterConfigurationStrings::config::input::devices::Up, Up);
			InputDevice->Up = DisplayClusterConfigurationJsonHelpers::FromString<EDisplayClusterConfigurationTrackerMapping>(Up);

			Config->Input->TrackerDevices.Emplace(CfgInput.Key, InputDevice);
		}
	}

	// Input bindings
	for (const auto& CfgInputBinding : CfgJson.InputBindings)
	{
		FDisplayClusterConfigurationInputBinding InputBinding;

		InputBinding.DeviceId = CfgInputBinding.Device;
		InputBinding.Channel = DisplayClusterHelpers::map::ExtractValueFromString(CfgInputBinding.Parameters, DisplayClusterConfigurationStrings::config::input::binding::BindChannel, (int32)INDEX_NONE);
		InputBinding.Key     = DisplayClusterHelpers::map::ExtractValue(CfgInputBinding.Parameters, DisplayClusterConfigurationStrings::config::input::binding::BindKey, FString());
		InputBinding.BindTo  = DisplayClusterHelpers::map::ExtractValue(CfgInputBinding.Parameters, DisplayClusterConfigurationStrings::config::input::binding::BindTo, FString());

		Config->Input->InputBinding.Add(InputBinding);
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
	if (!(Config && Config->Scene && Config->Input && Config->Cluster))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("nullptr detected in the configuration data"));
		return false;
	}

	FDisplayClusterConfigurationJsonNdisplay& Json = JsonData.nDisplay;

	// Info
	Json.Description = Config->Info.Description;
	Json.Version     = Config->Info.Version;

	// Scene
	{
		// Xforms
		for (const auto& CfgComp : Config->Scene->Xforms)
		{
			FDisplayClusterConfigurationJsonSceneComponentXform Xform;

			// General
			Xform.Parent   = CfgComp.Value->ParentId;
			Xform.Location = FDisplayClusterConfigurationJsonVector::FromVector(CfgComp.Value->Location / 100.f);
			Xform.Rotation = FDisplayClusterConfigurationJsonRotator::FromRotator(CfgComp.Value->Rotation);
			Xform.TrackerId      = CfgComp.Value->TrackerId;
			Xform.TrackerChannel = CfgComp.Value->TrackerChannel;

			Json.Scene.Xforms.Emplace(CfgComp.Key, Xform);
		}

		// Screens
		for (const auto& CfgComp : Config->Scene->Screens)
		{
			FDisplayClusterConfigurationJsonSceneComponentScreen Screen;

			// General
			Screen.Parent   = CfgComp.Value->ParentId;
			Screen.Location = FDisplayClusterConfigurationJsonVector::FromVector(CfgComp.Value->Location / 100.f);
			Screen.Rotation = FDisplayClusterConfigurationJsonRotator::FromRotator(CfgComp.Value->Rotation);
			Screen.TrackerId      = CfgComp.Value->TrackerId;
			Screen.TrackerChannel = CfgComp.Value->TrackerChannel;
			// Screen specific
			Screen.Size = FDisplayClusterConfigurationJsonSizeFloat::FromVector(CfgComp.Value->Size / 100.f);

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
			Camera.TrackerId      = CfgComp.Value->TrackerId;
			Camera.TrackerChannel = CfgComp.Value->TrackerChannel;
			// Camera specific
			Camera.InterpupillaryDistance = CfgComp.Value->InterpupillaryDistance / 100.f;
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
			Mesh.TrackerId      = CfgComp.Value->TrackerId;
			Mesh.TrackerChannel = CfgComp.Value->TrackerChannel;
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
				Viewport.BufferRatio = CfgViewport.Value->BufferRatio;
				Viewport.AllowCrossGPUTransfer = CfgViewport.Value->bAllowCrossGPUTransfer;

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

	// Input
	{
		// Analog devices
		for (const auto& CfgInputAnalog : Config->Input->AnalogDevices)
		{
			FDisplayClusterConfigurationJsonInputDevice AnalogDevice;

			AnalogDevice.Type = DisplayClusterConfigurationStrings::config::input::devices::VrpnDeviceAnalog;
			AnalogDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Address, CfgInputAnalog.Value->Address);

			const FString Remapping = DisplayClusterHelpers::str::MapToStr(CfgInputAnalog.Value->ChannelRemapping, DisplayClusterStrings::common::ArrayValSeparator, FString(":"), false);
			AnalogDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Remapping, Remapping);

			Json.Input.Emplace(CfgInputAnalog.Key, AnalogDevice);
		}

		// Button devices
		for (const auto& CfgInputButton : Config->Input->ButtonDevices)
		{
			FDisplayClusterConfigurationJsonInputDevice ButtonDevice;

			ButtonDevice.Type = DisplayClusterConfigurationStrings::config::input::devices::VrpnDeviceButton;
			ButtonDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Address, CfgInputButton.Value->Address);

			const FString Remapping = DisplayClusterHelpers::str::MapToStr(CfgInputButton.Value->ChannelRemapping, DisplayClusterStrings::common::ArrayValSeparator, FString(":"), false);
			ButtonDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Remapping, Remapping);

			Json.Input.Emplace(CfgInputButton.Key, ButtonDevice);
		}

		// Keyboard devices
		for (const auto& CfgInputKb : Config->Input->KeyboardDevices)
		{
			FDisplayClusterConfigurationJsonInputDevice KbDevice;

			KbDevice.Type = DisplayClusterConfigurationStrings::config::input::devices::VrpnDeviceKeyboard;
			KbDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Address, CfgInputKb.Value->Address);

			const FString Remapping = DisplayClusterHelpers::str::MapToStr(CfgInputKb.Value->ChannelRemapping, DisplayClusterStrings::common::ArrayValSeparator, FString(":"), false);
			KbDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Remapping, Remapping);

			const FString Reflection = DisplayClusterConfigurationJsonHelpers::ToString< EDisplayClusterConfigurationKeyboardReflectionType>(CfgInputKb.Value->ReflectionType);
			KbDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::ReflectType, Reflection);

			Json.Input.Emplace(CfgInputKb.Key, KbDevice);
		}

		// Tracker devices
		for (const auto& CfgInputTracker : Config->Input->TrackerDevices)
		{
			FDisplayClusterConfigurationJsonInputDevice TrackerDevice;

			TrackerDevice.Type = DisplayClusterConfigurationStrings::config::input::devices::VrpnDeviceTracker;
			TrackerDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Address, CfgInputTracker.Value->Address);

			const FString Remapping = DisplayClusterHelpers::str::MapToStr(CfgInputTracker.Value->ChannelRemapping, DisplayClusterStrings::common::ArrayValSeparator, FString(":"), false);
			TrackerDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Remapping, Remapping);

			const TArray<float> Location = { CfgInputTracker.Value->OriginLocation.X / 100.f, CfgInputTracker.Value->OriginLocation.Y / 100.f, CfgInputTracker.Value->OriginLocation.Z / 100.f };
			const FString LocationStr = DisplayClusterHelpers::str::ArrayToStr(Location);
			TrackerDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::OriginLocation, LocationStr);

			const TArray<float> Rotation = { CfgInputTracker.Value->OriginRotation.Pitch, CfgInputTracker.Value->OriginRotation.Yaw, CfgInputTracker.Value->OriginRotation.Roll };
			const FString RotationStr = DisplayClusterHelpers::str::ArrayToStr(Rotation);
			TrackerDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::OriginRotation, RotationStr);

			TrackerDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::OriginComponent, CfgInputTracker.Value->OriginComponent);
			
			const FString Front = DisplayClusterConfigurationJsonHelpers::ToString<EDisplayClusterConfigurationTrackerMapping>(CfgInputTracker.Value->Front);
			TrackerDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Front, Front);

			const FString Right = DisplayClusterConfigurationJsonHelpers::ToString<EDisplayClusterConfigurationTrackerMapping>(CfgInputTracker.Value->Right);
			TrackerDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Right, Right);

			const FString Up = DisplayClusterConfigurationJsonHelpers::ToString<EDisplayClusterConfigurationTrackerMapping>(CfgInputTracker.Value->Up);
			TrackerDevice.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::devices::Up, Up);

			Json.Input.Emplace(CfgInputTracker.Key, TrackerDevice);
		}
	}

	// Input bindings
	for (const auto& CfgInputBinding : Config->Input->InputBinding)
	{
		FDisplayClusterConfigurationJsonInputBinding InputBinding;

		InputBinding.Device = CfgInputBinding.DeviceId;
		InputBinding.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::binding::BindChannel, DisplayClusterTypesConverter::template ToString(CfgInputBinding.Channel));
		InputBinding.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::binding::BindKey,     CfgInputBinding.Key);
		InputBinding.Parameters.Emplace(DisplayClusterConfigurationStrings::config::input::binding::BindTo,      CfgInputBinding.BindTo);

		Json.InputBindings.Add(InputBinding);
	}

	// Custom parameters
	Json.CustomParameters = Config->CustomParameters;

	// Diagnostics
	Json.Diagnostics.SimulateLag = Config->Diagnostics.bSimulateLag;
	Json.Diagnostics.MinLagTime  = Config->Diagnostics.MinLagTime;
	Json.Diagnostics.MaxLagTime  = Config->Diagnostics.MaxLagTime;

	return true;
}
