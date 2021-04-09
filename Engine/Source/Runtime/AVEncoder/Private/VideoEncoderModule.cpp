// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderCommon.h"
#include "Modules/ModuleManager.h"

#include "VideoEncoderFactory.h"


//namespace AVEncoder
//{

class FAVEncoderModule : public IModuleInterface
{
public:
	// Factories initialization is done lazily whenever some code uses the Factory API.
	// This is so because factories needs some RHI stuff initialized

	void ShutdownModule()
	{
		AVEncoder::FVideoEncoderFactory::Shutdown();

	}
};

//} /* namespace AVEncoder */

IMPLEMENT_MODULE(FAVEncoderModule, AVEncoder);
