// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LinuxAArch64NoEditorTargetPlatformModule.cpp: Implements the FLinuxAArch64NoEditorTargetPlatformModule class.
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
class FLinuxAArch64NoEditorTargetPlatformModule
	: public ITargetPlatformModule
{
public:

	virtual ~FLinuxAArch64NoEditorTargetPlatformModule( )
	{
		Singleton = NULL;
	}

	virtual ITargetPlatform* GetTargetPlatform( )
	{
		if (Singleton == NULL && TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, false, true> >::IsUsable())
		{
			Singleton = new TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, false, true> >();
		}

		return Singleton;
	}
};


IMPLEMENT_MODULE(FLinuxAArch64NoEditorTargetPlatformModule, LinuxAArch64NoEditorTargetPlatform);
