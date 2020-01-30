// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "AVEncoder.h"
#include "AVEncoderCommon.h"

class FAVEncoderModule : public IModuleInterface
{
public:
	// Factories initialization is done lazily whenever some code uses the Factory API.
	// This is so because factories needs some RHI stuff initialized
};

IMPLEMENT_MODULE(FAVEncoderModule, AVEncoder);

