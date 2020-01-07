// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LinuxAArch64TargetPlatformModule.cpp: Implements the FLinuxAArch64ServerTargetPlatformModule class.
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
 * Module for the Linux AArch64 Server target platform.
 */
class FLinuxAArch64ServerTargetPlatformModule
	: public ITargetPlatformModule
{
public:

	virtual ~FLinuxAArch64ServerTargetPlatformModule( )
	{
		Singleton = NULL;
	}

	virtual ITargetPlatform* GetTargetPlatform( )
	{
		if (Singleton == NULL && TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, false, true> >::IsUsable())
		{
			Singleton = new TLinuxTargetPlatform<FLinuxPlatformProperties<false, true, false, true> >();
		}
		
		return Singleton;
	}
};


IMPLEMENT_MODULE( FLinuxAArch64ServerTargetPlatformModule, LinuxAArch64ServerTargetPlatform);
