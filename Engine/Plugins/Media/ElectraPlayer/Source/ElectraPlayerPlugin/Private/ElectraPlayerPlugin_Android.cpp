// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"

#if PLATFORM_ANDROID

#include <ElectraPlayerPlugin.h>


class FElectraPlayerResourceDelegate : public IElectraPlayerResourceDelegate
{
public:
	FElectraPlayerResourceDelegate(FElectraPlayerPlugin* InOwner) : Owner(InOwner) {}

	virtual jobject GetCodecSurface() override
	{
		return (jobject)Owner->OutputTexturePool.GetCodecSurface();
	}

private:
	FElectraPlayerPlugin* Owner;
};


IElectraPlayerResourceDelegate* FElectraPlayerPlugin::PlatformCreatePlayerResourceDelegate()
{
	return new FElectraPlayerResourceDelegate(this);
}

void FElectraPlayerPlugin::PlatformSetupResourceParams(Electra::FParamDict& Params)
{
}

#endif // PLATFORM_ANDROID
