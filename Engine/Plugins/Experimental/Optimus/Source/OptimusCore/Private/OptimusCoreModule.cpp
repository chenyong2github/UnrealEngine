// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusCoreModule.h"

#include "OptimusObjectVersion.h"

#include "UObject/DevObjectVersion.h"

// Unique serialization id for Optimus .
const FGuid FOptimusObjectVersion::GUID(0x93ede1aa, 0x10ca7375, 0x4df98a28, 0x49b157a0);

static FDevVersionRegistration GRegisterOptimusObjectVersion(FOptimusObjectVersion::GUID, FOptimusObjectVersion::LatestVersion, TEXT("Dev-Optimus"));


void FOptimusCoreModule::StartupModule()
{

}


void FOptimusCoreModule::ShutdownModule()
{

}

IMPLEMENT_MODULE(FOptimusCoreModule, OptimusCore)
