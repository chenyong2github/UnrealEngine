// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "PlatformFeatures.h"
#include "IMagicLeapSaveGameSystem.h"

class FLuminPlatformFeatures : public IPlatformFeaturesModule
{
public:
	virtual class ISaveGameSystem* GetSaveGameSystem() override
	{
		static FMagicLeapSaveGameSystem IMagicLeapSaveGameSystem;
		return &IMagicLeapSaveGameSystem;
	}
};

IMPLEMENT_MODULE(FLuminPlatformFeatures, LuminPlatformFeatures);
