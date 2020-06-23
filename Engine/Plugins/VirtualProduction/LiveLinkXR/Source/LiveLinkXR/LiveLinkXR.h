// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLiveLinkXRModule : public IModuleInterface
{
public:

	static FLiveLinkXRModule& Get()
	{
		return FModuleManager::Get().LoadModuleChecked<FLiveLinkXRModule>(TEXT("LiveLinkXR"));
	}

	static bool WasShutDown()
	{
		return bWasShutdown;
	}

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Gets the settings object used for LiveLinkXR sources. */
	LIVELINKXR_API class ULiveLinkXRSettingsObject const* const GetSettings() const;

private:

	// We explicitly use a map of Pointer to WeakPointers instead of a set.
	// This is because we may unregister sources from FLiveLinkXR source.
	// At that point, the Weak Pointer would already be invalid and may not hash correctly.
	mutable TMap<class FLiveLinkXRSource*, TWeakPtr<class FLiveLinkXRSource>> ActiveSources;

	// Whether or not this module has already been shutdown.
	static bool bWasShutdown;
};
