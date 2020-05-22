// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITargetPlatformModule.h"
#include "Common/TargetPlatformBase.h"
#include "LuminTargetPlatform.h"

/**
 * Module for the Android target platform.
 */
class FLuminTargetPlatformModule : public ITargetPlatformModule
{
public:

	virtual void GetTargetPlatforms(TArray<ITargetPlatform*>& TargetPlatforms) override
	{
		if (FLuminTargetPlatform::IsUsable())
		{
			TargetPlatforms.Add(new FLuminTargetPlatform(false));
			TargetPlatforms.Add(new FLuminTargetPlatform(true));
		}
	}
};


IMPLEMENT_MODULE( FLuminTargetPlatformModule, LuminTargetPlatform);
