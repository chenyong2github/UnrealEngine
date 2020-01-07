// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "XXXTargetPlatform.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITargetPlatformModule.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FXXXTargetPlatformModule"

// Holds the target platform singleton.
static ITargetPlatform* Singleton = nullptr;


/**
 * Module for the XXX target platform.
 */
class FXXXTargetPlatformModule : public ITargetPlatformModule
{
public:

	/** Destructor. */
	~FXXXTargetPlatformModule()
	{
		delete Singleton;
		Singleton = nullptr;
	}
	
public:

	// ITargetPlatformModule interface

	virtual ITargetPlatform* GetTargetPlatform() override
	{
		if (Singleton == nullptr)
		{
			Singleton = new FXXXTargetPlatform();
		}

		return Singleton;
	}

public:

	// IModuleInterface interface

	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};


#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE( FXXXTargetPlatformModule, XXXTargetPlatform);
