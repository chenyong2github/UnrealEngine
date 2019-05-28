// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

class HoloLensTargetPlatform : public FHoloLensTargetPlatform
{
public:
	HoloLensTargetPlatform()
	{
	}

	virtual FText GetVariantDisplayName() const override
	{
		return LOCTEXT("HoloLensVariantDisplayName", "HoloLens");
	}

	virtual float GetVariantPriority() const override
	{
		return 1.0f;
	}

protected:
	virtual bool SupportsDevice(FName DeviceType, bool DeviceIs64Bits)
	{
		return true;
	}
};



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

			HoloLensTargetSingleton = new HoloLensTargetPlatform();
		}
		
		return HoloLensTargetSingleton;
	}
};


#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE(FHoloLensTargetPlatformModule, HoloLensTargetPlatform);


#include "Windows/HideWindowsPlatformTypes.h"
