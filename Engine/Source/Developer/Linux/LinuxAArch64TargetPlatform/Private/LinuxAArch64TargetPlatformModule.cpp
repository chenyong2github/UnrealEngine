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
 * Module for the Linux target platforms
 */
class FLinuxAArch64TargetPlatformModule
	: public ITargetPlatformModule
{
public:

	virtual void GetTargetPlatforms(TArray<ITargetPlatform*>& TargetPlatforms) override
	{
		// Server TP
		if (TLinuxTargetPlatform<FLinuxPlatformProperties<false, true, false, true> >::IsUsable())
		{
			TargetPlatforms.Add(new TLinuxTargetPlatform<FLinuxPlatformProperties<false, true, false, true> >());
		}
		// Game TP
		if (TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, false, true> >::IsUsable())
		{
			TargetPlatforms.Add(new TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, false, true> >());
		}
		// Client TP
		if (TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, true, true> >::IsUsable())
		{
			TargetPlatforms.Add(new TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, true, true> >());
		}
	}
};


IMPLEMENT_MODULE(FLinuxAArch64TargetPlatformModule, LinuxAArch64TargetPlatform);
