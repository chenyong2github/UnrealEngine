// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FSteamVRInputModule : public IModuleInterface
{
public:

	/* IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

#if WITH_EDITOR
	void AddEditorSettings();
	void RemoveEditorSettings();
#endif

	static inline IModuleInterface& Get()
	{
		return FModuleManager::LoadModuleChecked<IModuleInterface>("SteamVRInput");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("SteamVRInput");
	}

private:
	/* Handle to the OpenVR Library */
	void*	OpenVRSDKHandle;
};
