// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "NNXCore.h"
#include "NNXRuntime.h"
#include "NNXRuntimeCPU.h"

//#include "ThirdPartyWarningDisabler.h"
//NNI_THIRD_PARTY_INCLUDES_START
//#undef check
//#undef TEXT
//#include "core/session/onnxruntime_cxx_api.h"
//NNI_THIRD_PARTY_INCLUDES_END


class FNNXRuntimeCPUModule: public IModuleInterface
{

public:
	NNX::IRuntime* CPURuntime{ nullptr };
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
