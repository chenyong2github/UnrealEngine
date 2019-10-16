// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LinuxAArch64ClientTargetPlatformModule.cpp: Implements the FLinuxAArch64ClientTargetPlatformModule class.
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
 * Module for the Linux target platform (without editor).
 */
class FLinuxAArch64ClientTargetPlatformModule
	: public ITargetPlatformModule
{
public:

	virtual ~FLinuxAArch64ClientTargetPlatformModule( )
	{
		Singleton = NULL;
	}

	virtual ITargetPlatform* GetTargetPlatform( )
	{
		if (Singleton == NULL && TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, true, true> >::IsUsable())
		{
 			Singleton = new TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, true, true> >();
		}

		return Singleton;
	}
};

// 
IMPLEMENT_MODULE(FLinuxAArch64ClientTargetPlatformModule, LinuxAArch64ClientTargetPlatform);
