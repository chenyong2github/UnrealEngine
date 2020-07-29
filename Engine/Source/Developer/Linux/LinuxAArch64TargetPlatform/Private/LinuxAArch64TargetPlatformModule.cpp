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
		// Game TP
		TargetPlatforms.Add(new TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, false, true> >());
		// Server TP
		TargetPlatforms.Add(new TLinuxTargetPlatform<FLinuxPlatformProperties<false, true, false, true> >());
		// Client TP
		TargetPlatforms.Add(new TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, true, true> >());
	}
};


IMPLEMENT_MODULE(FLinuxAArch64TargetPlatformModule, LinuxAArch64TargetPlatform);
