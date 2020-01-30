// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LinuxTargetPlatformModule.cpp: Implements the FLinuxServerTargetPlatformModule class.
=============================================================================*/

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Interfaces/ITargetPlatformModule.h"

#include "LinuxTargetDevice.h"
#include "LinuxTargetPlatform.h"

/**
 * Holds the target platform singleton.
 */
static ITargetPlatform* Singleton = NULL;


/**
 * Module for the Android target platform.
 */
class FLinuxServerTargetPlatformModule
	: public ITargetPlatformModule
{
public:

	virtual ~FLinuxServerTargetPlatformModule( )
	{
		Singleton = NULL;
	}

	virtual ITargetPlatform* GetTargetPlatform( )
	{
		if (Singleton == NULL && TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, false, false> >::IsUsable())
		{
			Singleton = new TLinuxTargetPlatform<FLinuxPlatformProperties<false, true, false, false> >();
		}
		
		return Singleton;
	}
};


IMPLEMENT_MODULE( FLinuxServerTargetPlatformModule, LinuxServerTargetPlatform);
