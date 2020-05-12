// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "Config/Parser/IDisplayClusterConfigParserListener.h"

#include "Misc/DisplayClusterBuildConfig.h"


class FDisplayClusterConfigParser;


/**
 * Config manager. Responsible for loading data from config file and providing with it to any other class.
 */
class FDisplayClusterConfigManager
	: public    IPDisplayClusterConfigManager
	, protected IDisplayClusterConfigParserListener
{
public:
	FDisplayClusterConfigManager();
	virtual ~FDisplayClusterConfigManager();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Init(EDisplayClusterOperationMode OperationMode) override;
	virtual void Release() override;
	virtual bool StartSession(const FString& ConfigPath, const FString& NodeId) override;
	virtual void EndSession() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterConfigManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TArray<FDisplayClusterConfigClusterNode> GetClusterNodes() const override;
	virtual int32 GetClusterNodesAmount() const override;
	virtual bool GetClusterNode(const FString& ClusterNodeID, FDisplayClusterConfigClusterNode& CfgClusterNode) const override;
	virtual bool GetMasterClusterNode(FDisplayClusterConfigClusterNode& CfgMasterNode) const override;

	virtual int32 GetWindowsAmount() const override;
	virtual TArray<FDisplayClusterConfigWindow> GetWindows() const override;
	virtual bool GetWindow(const FString& WindowID, FDisplayClusterConfigWindow& CfgWindow) const override;
	virtual bool GetMasterWindow(FDisplayClusterConfigWindow& Window) const override;

	virtual TArray<FDisplayClusterConfigScreen> GetScreens() const override;
	virtual int32 GetScreensAmount() const override;
	virtual bool GetScreen(const FString& ScreenID, FDisplayClusterConfigScreen& CfgScreen) const override;

	virtual TArray<FDisplayClusterConfigCamera> GetCameras() const override;
	virtual int32 GetCamerasAmount() const override;
	virtual bool GetCamera(const FString& CameraID, FDisplayClusterConfigCamera& CfgCamera) const override;

	virtual TArray<FDisplayClusterConfigViewport> GetViewports() const override;
	virtual int32 GetViewportsAmount() const override;
	virtual bool GetViewport(const FString& ViewportID, FDisplayClusterConfigViewport& CfgViewport) const override;

	virtual TArray<FDisplayClusterConfigPostprocess> GetPostprocess() const override;
	virtual int32 GetPostprocessAmount() const override;
	virtual bool GetPostprocess(const FString& PostprocessID, FDisplayClusterConfigPostprocess& CfgPostprocess) const override;

	virtual TArray<FDisplayClusterConfigSceneNode> GetSceneNodes() const override;
	virtual int32 GetSceneNodesAmount() const override;
	virtual bool GetSceneNode(const FString& SceneNodeID, FDisplayClusterConfigSceneNode& CfgSceneNode) const override;

	virtual TArray<FDisplayClusterConfigInput> GetInputDevices() const override;
	virtual int32 GetInputDevicesAmount() const override;
	virtual bool GetInputDevice(const FString& InputDeviceID, FDisplayClusterConfigInput& CfgInput) const override;

	virtual TArray<FDisplayClusterConfigInputSetup> GetInputSetupRecords() const override;
	virtual bool GetInputSetupRecord(const FString& InputSetupID, FDisplayClusterConfigInputSetup& CfgInputSetup) const override;

	virtual TArray<FDisplayClusterConfigProjection> GetProjections() const override;
	virtual bool GetProjection(const FString& ProjectionID, FDisplayClusterConfigProjection& CfgProjection) const override;

	virtual FDisplayClusterConfigGeneral GetConfigGeneral() const override
	{ return CfgGeneral; }

	virtual FDisplayClusterConfigStereo  GetConfigStereo() const override
	{ return CfgStereo; }

	virtual FDisplayClusterConfigRender  GetConfigRender() const override
	{ return CfgRender; }

	virtual FDisplayClusterConfigNvidia  GetConfigNvidia() const override
	{ return CfgNvidia; }

	virtual FDisplayClusterConfigNetwork GetConfigNetwork() const override
	{ return CfgNetwork; }

	virtual FDisplayClusterConfigDebug   GetConfigDebug() const override
	{ return CfgDebug; }

	virtual FDisplayClusterConfigCustom  GetConfigCustom() const override
	{ return CfgCustom; }

	virtual FString GetFullPathToFile(const FString& FileName) const override;
	virtual FString GetFullPathToNewFile(const FString& FileName) const override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterConfigManager
	//////////////////////////////////////////////////////////////////////////////////////////////
#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
	virtual bool IsRunningDebugAuto() const override
	{ return bIsDebugAuto; }
#endif

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterConfigParserListener
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void AddInfo(const FDisplayClusterConfigInfo& InCfgInfo) override;
	virtual void AddClusterNode(const FDisplayClusterConfigClusterNode& InCfgCNode) override;
	virtual void AddWindow(const FDisplayClusterConfigWindow& InCfgWindow) override;
	virtual void AddScreen(const FDisplayClusterConfigScreen& InCfgScreen) override;
	virtual void AddViewport(const FDisplayClusterConfigViewport& InCfgViewport) override;
	virtual void AddPostprocess(const FDisplayClusterConfigPostprocess& InCfgPostprocess) override;
	virtual void AddCamera(const FDisplayClusterConfigCamera& InCfgCamera) override;
	virtual void AddSceneNode(const FDisplayClusterConfigSceneNode& InCfgSNode)  override;
	virtual void AddGeneral(const FDisplayClusterConfigGeneral& InCfgGeneral)  override;
	virtual void AddRender(const FDisplayClusterConfigRender& InCfgRender)  override;
	virtual void AddNvidia(const FDisplayClusterConfigNvidia& InCfgNvidia)  override;
	virtual void AddStereo(const FDisplayClusterConfigStereo& InCfgStereo)  override;
	virtual void AddNetwork(const FDisplayClusterConfigNetwork& InCfgNetwork) override;
	virtual void AddDebug(const FDisplayClusterConfigDebug& InCfgDebug)  override;
	virtual void AddInput(const FDisplayClusterConfigInput& InCfgInput)  override;
	virtual void AddInputSetup(const FDisplayClusterConfigInputSetup& InCfgInputSetup) override;
	virtual void AddCustom(const FDisplayClusterConfigCustom& InCfgCustom) override;
	virtual void AddProjection(const FDisplayClusterConfigProjection& InCfgProjection) override;

private:
	enum class EConfigFileType
	{
		Unknown,
#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
		DebugAuto,
#endif
		Text,
		Xml
	};

	template <typename DataType>
	bool GetItem(const TArray<DataType>& Container, const FString& ID, DataType& Item, const FString& LogHeader) const;

	EConfigFileType GetConfigFileType(const FString& InConfigPath) const;
	bool LoadConfig(const FString& InConfigPath);
	void ResetConfigData();

private:
	FString ConfigPath;
	FString ClusterNodeId;

	TArray<FDisplayClusterConfigClusterNode> CfgClusterNodes;
	TArray<FDisplayClusterConfigWindow>      CfgWindows;
	TArray<FDisplayClusterConfigScreen>      CfgScreens;
	TArray<FDisplayClusterConfigViewport>    CfgViewports;
	TArray<FDisplayClusterConfigPostprocess> CfgPostprocess;
	TArray<FDisplayClusterConfigCamera>      CfgCameras;
	TArray<FDisplayClusterConfigSceneNode>   CfgSceneNodes;
	TArray<FDisplayClusterConfigInput>       CfgInputDevices;
	TArray<FDisplayClusterConfigInputSetup>  CfgInputSetupRecords;
	TArray<FDisplayClusterConfigProjection>  CfgProjections;

	FDisplayClusterConfigInfo    CfgInfo;
	FDisplayClusterConfigGeneral CfgGeneral;
	FDisplayClusterConfigStereo  CfgStereo;
	FDisplayClusterConfigRender  CfgRender;
	FDisplayClusterConfigNvidia  CfgNvidia;
	FDisplayClusterConfigNetwork CfgNetwork;
	FDisplayClusterConfigDebug   CfgDebug;
	FDisplayClusterConfigCustom  CfgCustom;

#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
	bool bIsDebugAuto = false;
#endif
};
