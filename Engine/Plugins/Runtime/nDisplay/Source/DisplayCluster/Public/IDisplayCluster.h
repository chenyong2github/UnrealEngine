// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

#include "DisplayClusterEnums.h"


class IDisplayClusterRenderManager;
class IDisplayClusterClusterManager;
class IDisplayClusterInputManager;
class IDisplayClusterConfigManager;
class IDisplayClusterGameManager;


/**
 * Public module interface
 */
class IDisplayCluster
	: public IModuleInterface
{
public:
	static constexpr auto ModuleName = TEXT("DisplayCluster");

	virtual ~IDisplayCluster() = 0
	{ }

	/**
	* Singleton-like access to this module's interface.  This is just for convenience!
	* Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	*
	* @return Returns singleton instance, loading the module on demand if needed
	*/
	static inline IDisplayCluster& Get()
	{
		//return FModuleManager::LoadModuleChecked<IDisplayCluster>(IDisplayCluster::ModuleName);
		return FModuleManager::GetModuleChecked<IDisplayCluster>(IDisplayCluster::ModuleName);
	}

	/**
	* Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	*
	* @return True if the module is loaded and ready to use
	*/
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(IDisplayCluster::ModuleName);
	}


	/**
	* Checks if the module has been initialized.
	*
	* @return Is initialized
	*/
	virtual bool IsModuleInitialized() const = 0;

	/**
	* Returns current operation mode.
	*
	* @return Display Cluster operation mode
	*/
	virtual EDisplayClusterOperationMode GetOperationMode() const = 0;

	/**
	* Access to the device manager.
	*
	* @return Current device manager or nullptr
	*/
	virtual IDisplayClusterRenderManager* GetRenderMgr() const = 0;

	/**
	* Access to the cluster manager.
	*
	* @return Current cluster manager or nullptr
	*/
	virtual IDisplayClusterClusterManager* GetClusterMgr() const = 0;

	/**
	* Access to the input manager.
	*
	* @return Current cluster manager or nullptr
	*/
	virtual IDisplayClusterInputManager* GetInputMgr() const = 0;

	/**
	* Access to the config manager.
	*
	* @return Current config manager or nullptr
	*/
	virtual IDisplayClusterConfigManager* GetConfigMgr() const = 0;

	/**
	* Access to the game manager.
	*
	* @return Current game manager or nullptr
	*/
	virtual IDisplayClusterGameManager* GetGameMgr() const = 0;


	/** Called on session start **/
	DECLARE_EVENT(IDisplayCluster, FDisplayClusterStartSessionEvent);
	virtual FDisplayClusterStartSessionEvent& OnDisplayClusterStartSession() = 0;

	/** Called on session end **/
	DECLARE_EVENT(IDisplayCluster, FDisplayClusterEndSessionEvent);
	virtual FDisplayClusterEndSessionEvent& OnDisplayClusterEndSession() = 0;

	/** Called on DisplayCluster StartFrame **/
	DECLARE_EVENT_OneParam(IDisplayCluster, FDisplayClusterStartFrameEvent, uint64);
	virtual FDisplayClusterStartFrameEvent& OnDisplayClusterStartFrame() = 0;

	/** Called on DisplayCluster EndFrame **/
	DECLARE_EVENT_OneParam(IDisplayCluster, FDisplayClusterEndFrameEvent, uint64);
	virtual FDisplayClusterEndFrameEvent& OnDisplayClusterEndFrame() = 0;

	/** Called on DisplayCluster PreTick **/
	DECLARE_EVENT(IDisplayCluster, FDisplayClusterPreTickEvent);
	virtual FDisplayClusterPreTickEvent& OnDisplayClusterPreTick() = 0;

	/** Called on DisplayCluster Tick **/
	DECLARE_EVENT(IDisplayCluster, FDisplayClusterTickEvent);
	virtual FDisplayClusterTickEvent& OnDisplayClusterTick() = 0;

	/** Called on DisplayCluster PostTick **/
	DECLARE_EVENT(IDisplayCluster, FDisplayClusterPostTickEvent);
	virtual FDisplayClusterPostTickEvent& OnDisplayClusterPostTick() = 0;
};
