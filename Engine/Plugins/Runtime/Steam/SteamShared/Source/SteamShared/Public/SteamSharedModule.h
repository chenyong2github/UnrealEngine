// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SteamSharedPackage.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#define LOADING_STEAM_CLIENT_LIBRARY_DYNAMICALLY		(PLATFORM_WINDOWS || PLATFORM_MAC || (PLATFORM_LINUX && !IS_MONOLITHIC))
#define LOADING_STEAM_SERVER_LIBRARY_DYNAMICALLY		(PLATFORM_WINDOWS || (PLATFORM_LINUX && !IS_MONOLITHIC) || PLATFORM_MAC)
#define LOADING_STEAM_LIBRARIES_DYNAMICALLY				(LOADING_STEAM_CLIENT_LIBRARY_DYNAMICALLY || LOADING_STEAM_SERVER_LIBRARY_DYNAMICALLY)

class STEAMSHARED_API FSteamSharedModule : public IModuleInterface
{
public:

	FSteamSharedModule() : 
		SteamDLLHandle(nullptr),
		SteamServerDLLHandle(nullptr),
		bForceLoadSteamClientDll(false),
		InstanceHandlerObserver(nullptr)
	{
	}

	virtual ~FSteamSharedModule() {}

	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// Due to the loading of the DLLs and how the Steamworks API is initialized, we cannot support dynamic reloading.
	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	/**
	 * Initializes Steam Client API and provides a handler that will keep the API valid for the lifetime of the
	 * the object. Several Handlers can be active at once. 
	 *
	 * @return A handler to the Steam Client API, use IsValid to check if the handle is initialized.
	 */
	TSharedPtr<class FSteamInstanceHandler> ObtainSteamInstanceHandle();
	
	/**
	 * Are the Steamworks Dlls loaded
	 */
	bool AreSteamDllsLoaded() const;
	
	/**
	 * The path to where the Steam binaries are stored, for use in debugging.
	 */
	FString GetSteamModulePath() const;

	/**
	 * If the module will be loading the client dlls for the dedicated server instance.
	 * Really only useful on Windows.
	 */
	bool IsLoadingServerClientDlls() const { return bForceLoadSteamClientDll; }

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline class FSteamSharedModule& Get()
	{
		return FModuleManager::LoadModuleChecked<class FSteamSharedModule>("SteamShared");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("SteamShared");
	}

private:

	/** Handle to the STEAM API dll */
	void* SteamDLLHandle;

	/** Handle to the STEAM dedicated server support dlls */
	void* SteamServerDLLHandle;

	/** If we force loaded the steamclient dlls due to launch flags */
	bool bForceLoadSteamClientDll;

	/** Object that holds the refcounted pointer that's given out */
	TWeakPtr<class FSteamInstanceHandler> InstanceHandlerObserver;

	/** Load the required modules for Steam */
	void LoadSteamModules();

	/** Unload the required modules for Steam */
	void UnloadSteamModules();
};

/** A simple instance handler that creates and uninitializes the SteamAPI automatically. */
class STEAMSHARED_API FSteamInstanceHandler
{
public:
	virtual ~FSteamInstanceHandler();

PACKAGE_SCOPE:
	/** This is mostly here for safety so that we don't try to clean up when we shouldn't */
	bool bInitialized;

	/** Initializes the Steamworks client API on call */	
	FSteamInstanceHandler(FSteamSharedModule* SteamInitializer);

private:
	FSteamInstanceHandler() : bInitialized(false)
	{
	}
};
