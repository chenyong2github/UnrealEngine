// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLensTargetDevice.h"
#include "Interfaces/ITargetPlatformModule.h"
#include "HoloLensTargetPlatform.h"
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "FHoloLensTargetPlatformModule"



/**
 * Holds the target platform singleton.
 */
static ITargetPlatform* HoloLensTargetSingleton = nullptr;


/**
 * Module for the HoloLens target platform.
 */
class FHoloLensTargetPlatformModule
	: public ITargetPlatformModule
{
public:

	/** Default constructor. */
	FHoloLensTargetPlatformModule( )
	{ }

	/** Destructor. */
	~FHoloLensTargetPlatformModule( )
	{
	}

public:
	
	// ITargetPlatformModule interface
	
	virtual ITargetPlatform* GetTargetPlatform( )
	{
		if (HoloLensTargetSingleton == nullptr)
		{
			//@todo HoloLens: Check for SDK?

			HoloLensTargetSingleton = new FHoloLensTargetPlatform();
		}
		
		return HoloLensTargetSingleton;
	}
};


#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE(FHoloLensTargetPlatformModule, HoloLensTargetPlatform);


#include "Windows/HideWindowsPlatformTypes.h"
