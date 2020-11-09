// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class  UDisplayClusterConfigurationData;
class  UDisplayClusterConfigurationClusterNode;
class  UDisplayClusterConfigurationViewport;
struct FDisplayClusterConfigurationPostprocess;
struct FDisplayClusterConfigurationProjection;

/**
 * Public config manager interface
 */
class IDisplayClusterConfigManager
{
public:
	virtual ~IDisplayClusterConfigManager() = 0
	{ }

public:
	// Returns current config data
	virtual const UDisplayClusterConfigurationData* GetConfig() const = 0;

public:
	// Returns path of the config file that is currently used
	virtual FString GetConfigPath() const = 0;

	// Returns ID of cluster node that is assigned to this application instance
	virtual FString GetLocalNodeId() const = 0;
	// Returns master node ID
	virtual FString GetMasterNodeId() const = 0;

	// Returns master node configuration data
	virtual const UDisplayClusterConfigurationClusterNode* GetMasterNode() const = 0;
	// Returns configuration data for cluster node that is assigned to this application instance
	virtual const UDisplayClusterConfigurationClusterNode* GetLocalNode() const = 0;
	// Returns configuration data for a specified local viewport 
	virtual const UDisplayClusterConfigurationViewport*    GetLocalViewport(const FString& ViewportId) const = 0;
	// Returns configuration data for a specified local postprocess operation
	virtual bool  GetLocalPostprocess(const FString& PostprocessId, FDisplayClusterConfigurationPostprocess& OutPostprocess) const = 0;
	// Returns configuration data for local projection policy
	virtual bool  GetLocalProjection(const FString& ViewportId, FDisplayClusterConfigurationProjection& OutProjection) const = 0;


	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// DEPRECATED
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual int32 GetClusterNodesAmount() const
	{
		return 0;
	}

	struct FDisplayClusterConfigClusterNode { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigClusterNode> GetClusterNodes() const
	{
		return TArray<FDisplayClusterConfigClusterNode>();
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetClusterNode(const FString& ClusterNodeId, FDisplayClusterConfigClusterNode& CfgClusterNode) const
	{
		return false;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetMasterClusterNode(FDisplayClusterConfigClusterNode& cnode) const
	{
		return false;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual int32 GetWindowsAmount() const
	{
		return 0;
	}
	
	struct FDisplayClusterConfigWindow { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigWindow> GetWindows() const
	{
		return TArray<FDisplayClusterConfigWindow>();
	}
	
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetWindow(const FString& WindowID, FDisplayClusterConfigWindow& CfgWindow) const
	{
		return false;
	}
	
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetMasterWindow(FDisplayClusterConfigWindow& CfgWindow) const
	{
		return false;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual int32 GetScreensAmount() const
	{
		return 0;
	}
	
	struct FDisplayClusterConfigScreen { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigScreen> GetScreens() const
	{
		return TArray<FDisplayClusterConfigScreen>();
	}
	
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetScreen(const FString& ScreenID, FDisplayClusterConfigScreen& CfgScreen) const
	{
		return false;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual int32 GetCamerasAmount() const
	{
		return 0;
	}
	
	struct FDisplayClusterConfigCamera { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigCamera> GetCameras() const
	{
		return TArray<FDisplayClusterConfigCamera>();
	}
	
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetCamera(const FString& CameraID, FDisplayClusterConfigCamera& CfgCamera) const
	{
		return false;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual int32 GetViewportsAmount() const
	{
		return 0;
	}

	struct FDisplayClusterConfigViewport { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigViewport> GetViewports() const
	{
		return TArray<FDisplayClusterConfigViewport>();
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetViewport(const FString& ViewportID, FDisplayClusterConfigViewport& CfgViewport) const
	{
		return false;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual int32 GetPostprocessAmount() const
	{
		return 0;
	}

	struct FDisplayClusterConfigPostprocess { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigPostprocess> GetPostprocess() const
	{
		return TArray<FDisplayClusterConfigPostprocess>();
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetPostprocess(const FString& PostprocessID, FDisplayClusterConfigPostprocess& CfgPostprocess) const
	{
		return false;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual int32 GetSceneNodesAmount() const
	{
		return 0;
	}

	struct FDisplayClusterConfigSceneNode { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigSceneNode> GetSceneNodes() const
	{
		return TArray<FDisplayClusterConfigSceneNode>();
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetSceneNode(const FString& SceneNodeID, FDisplayClusterConfigSceneNode& CfgSceneNode) const
	{
		return false;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual int32 GetInputDevicesAmount() const
	{
		return 0;
	}

	struct FDisplayClusterConfigInput { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigInput> GetInputDevices() const
	{
		return TArray<FDisplayClusterConfigInput>();
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetInputDevice(const FString& InputDeviceID, FDisplayClusterConfigInput& CfgInputDevice) const
	{
		return false;
	}

	struct FDisplayClusterConfigInputSetup { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigInputSetup> GetInputSetupRecords() const
	{
		return TArray<FDisplayClusterConfigInputSetup>();
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetInputSetupRecord(const FString& InputSetupID, FDisplayClusterConfigInputSetup& CfgInputSetup) const
	{
		return false;
	}

	struct FDisplayClusterConfigProjection { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual TArray<FDisplayClusterConfigProjection> GetProjections() const
	{
		return TArray<FDisplayClusterConfigProjection>();
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual bool GetProjection(const FString& ProjectionID, FDisplayClusterConfigProjection& CfgProjection) const
	{
		return false;
	}

	struct FDisplayClusterConfigGeneral { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual FDisplayClusterConfigGeneral GetConfigGeneral() const
	{
		return FDisplayClusterConfigGeneral();
	}

	struct FDisplayClusterConfigStereo { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual FDisplayClusterConfigStereo  GetConfigStereo() const
	{
		return FDisplayClusterConfigStereo();
	}

	struct FDisplayClusterConfigRender { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual FDisplayClusterConfigRender  GetConfigRender() const
	{
		return FDisplayClusterConfigRender();
	}

	struct FDisplayClusterConfigNvidia { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual FDisplayClusterConfigNvidia  GetConfigNvidia() const
	{
		return FDisplayClusterConfigNvidia();
	}

	struct FDisplayClusterConfigNetwork { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual FDisplayClusterConfigNetwork GetConfigNetwork() const
	{
		return FDisplayClusterConfigNetwork();
	}

	struct FDisplayClusterConfigDebug { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual FDisplayClusterConfigDebug GetConfigDebug()   const
	{
		return FDisplayClusterConfigDebug();
	}

	struct FDisplayClusterConfigCustom { };
	UE_DEPRECATED(4.26, "This feature is no longer supported. Use GetConfig to retrieve full configuration info.")
	virtual FDisplayClusterConfigCustom  GetConfigCustom()  const
	{
		return FDisplayClusterConfigCustom();
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Refer DisplayClusterHelpers::config::GetFullPathForConfigResource")
	virtual FString GetFullPathToFile(const FString& FileName) const
	{
		return FString();
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported. Refer DisplayClusterHelpers::config::GetFullPathForConfigResource")
	virtual FString GetFullPathToNewFile(const FString& FileName) const
	{
		return FString();
	}
};
