// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterConfigTypes.h"


/**
 * Public config manager interface
 */
class IDisplayClusterConfigManager
{
public:
	virtual ~IDisplayClusterConfigManager() = 0
	{ }

	virtual int32 GetClusterNodesAmount() const = 0;
	virtual TArray<FDisplayClusterConfigClusterNode> GetClusterNodes() const = 0;
	virtual bool GetClusterNode(const FString& id, FDisplayClusterConfigClusterNode& cnode) const = 0;
	virtual bool GetMasterClusterNode(FDisplayClusterConfigClusterNode& cnode) const = 0;

	virtual int32 GetWindowsAmount() const = 0;
	virtual TArray<FDisplayClusterConfigWindow> GetWindows() const = 0;
	virtual bool GetWindow(const FString& ID, FDisplayClusterConfigWindow& Window) const = 0;
	virtual bool GetMasterWindow(FDisplayClusterConfigWindow& Window) const = 0;

	virtual int32 GetScreensAmount() const = 0;
	virtual TArray<FDisplayClusterConfigScreen> GetScreens() const = 0;
	virtual bool GetScreen(const FString& id, FDisplayClusterConfigScreen& screen) const = 0;

	virtual int32 GetCamerasAmount() const = 0;
	virtual TArray<FDisplayClusterConfigCamera> GetCameras() const = 0;
	virtual bool GetCamera(const FString& id, FDisplayClusterConfigCamera& camera) const = 0;

	virtual int32 GetViewportsAmount() const = 0;
	virtual TArray<FDisplayClusterConfigViewport> GetViewports() const = 0;
	virtual bool GetViewport(const FString& id, FDisplayClusterConfigViewport& viewport) const = 0;

	virtual int32 GetPostprocessAmount() const = 0;
	virtual TArray<FDisplayClusterConfigPostprocess> GetPostprocess() const = 0;
	virtual bool GetPostprocess(const FString& id, FDisplayClusterConfigPostprocess& postprocess) const = 0;

	virtual int32 GetSceneNodesAmount() const = 0;
	virtual TArray<FDisplayClusterConfigSceneNode> GetSceneNodes() const = 0;
	virtual bool GetSceneNode(const FString& id, FDisplayClusterConfigSceneNode& snode) const = 0;

	virtual int32 GetInputDevicesAmount() const = 0;
	virtual TArray<FDisplayClusterConfigInput> GetInputDevices() const = 0;
	virtual bool GetInputDevice(const FString& id, FDisplayClusterConfigInput& input) const = 0;

	virtual TArray<FDisplayClusterConfigInputSetup> GetInputSetupRecords() const = 0;
	virtual bool GetInputSetupRecord(const FString& id, FDisplayClusterConfigInputSetup& input) const = 0;

	virtual TArray<FDisplayClusterConfigProjection> GetProjections() const = 0;
	virtual bool GetProjection(const FString& id, FDisplayClusterConfigProjection& projection) const = 0;

	virtual FDisplayClusterConfigGeneral GetConfigGeneral() const = 0;
	virtual FDisplayClusterConfigStereo  GetConfigStereo()  const = 0;
	virtual FDisplayClusterConfigRender  GetConfigRender()  const = 0;
	virtual FDisplayClusterConfigNetwork GetConfigNetwork() const = 0;
	virtual FDisplayClusterConfigDebug   GetConfigDebug()   const = 0;
	virtual FDisplayClusterConfigCustom  GetConfigCustom()  const = 0;

	virtual FString GetFullPathToFile(const FString& FileName) const = 0;
};
